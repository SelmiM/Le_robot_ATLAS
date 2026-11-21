#pragma once
#include <cstdio>
#include <cstring>
#include "Config.h"
#include "Clock.h"
#include "Radio.h"
#include "Camera.h"
#include "HttpRequest.h"
#include "WebFiles.h"   // generated from robot/web/ at build time, see CMakeLists.txt

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

/**
 * HTTP transport for a phone on the same WiFi network as the robot:
 *
 *     http://<robot-ip>/                 the control page (robot/web/)
 *
 * The page uses these routes:
 *
 *     POST /api/command                  body: one command per line, the
 *                                        same JSON as over the USB cable
 *     GET  /api/lines?after=N            every line the robot has sent
 *                                        since line N, one per line
 *
 * Both transports carry the same newline-delimited JSON protocol. Try the
 * routes from a computer on the same network or with client/fakerobot.js:
 *
 *     curl -d '{"left":200,"right":200}' http://<robot-ip>/api/command
 *     curl http://<robot-ip>/api/lines
 *
 * HTTP cannot push lines to the browser, so the page polls for numbered
 * telemetry lines. connected() becomes false after WEB_TIMEOUT_MS without a
 * request; Robot.h then stops the wheels.
 * lwIP callbacks run inside poll() via pico_cyw43_arch_lwip_poll. This keeps
 * access to robot state in the main loop rather than in an interrupt.
 */
class WebRadio : public Radio {
public:
  // The camera, if there is one, is served at /snapshot.pgm. It lives inside
  // the Robot, which is built after the radio, so it is handed over later.
  void serveCamera(Camera& camera) { _camera = &camera; }

  bool begin() override {
    if (cyw43_arch_init() != 0) return false;
    cyw43_arch_enable_sta_mode();   // join a network, do not create one

    // The SDK turns on power saving by default: the chip sleeps between
    // beacons and the access point holds our packets until it wakes. Requests
    // arrive late or not at all, and the page sees the link drop. The radio
    // draws a few tens of mA more without it, little next to the motors.
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

    // This is the one place the robot is allowed to sit and wait, because
    // nothing can move until it finishes: the motors are off and the main
    // loop has not started yet.
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK,
                                           CONNECT_TIMEOUT_MS) != 0) {
      return false;
    }

    return startListening();
  }

  // Lets lwIP process whatever the radio chip has received; every callback
  // in this file runs inside cyw43_arch_poll() and nowhere else. Then some
  // housekeeping: hang up on browsers that have gone quiet, so their slot is
  // free for the next one.
  void poll() override {
    cyw43_arch_poll();
    keepLinkUp();

    for (Connection& c : _connections) {
      if (!c.pcb) continue;
      uint32_t quietMs = nowMs() - c.lastActiveMs;
      if (c.responding && quietMs > STALL_ABORT_MS) {
        abortConnection(c);
      } else if (c.closing || (!c.responding && quietMs > IDLE_CLOSE_MS)) {
        closeConnection(c);
      }
    }
  }

  bool connected() override {
    return _heardFrom && nowMs() - _lastRequestMs < WEB_TIMEOUT_MS;
  }

  bool receive(char* line, size_t maxLength) override {
    if (_commandCount == 0) return false;

    strncpy(line, _commands[_commandFirst], maxLength - 1);
    line[maxLength - 1] = '\0';
    _commandFirst = (_commandFirst + 1) % COMMANDS_KEPT;
    _commandCount--;
    return true;
  }

  // Nobody is listening at the moment we send, so we keep it until someone
  // asks. The oldest line is overwritten when the list is full: telemetry is
  // disposable, and a page that stopped asking for this long has bigger
  // problems than a missing line.
  void send(const char* line) override {
    _lastId++;
    Line& slot = _lines[_lastId % LINES_KEPT];
    slot.id = _lastId;
    strncpy(slot.text, line, MAX_LINE - 1);
    slot.text[MAX_LINE - 1] = '\0';
  }

  const char* describe() override {
    snprintf(_description, sizeof(_description), "http://%s/",
             ip4addr_ntoa(netif_ip4_addr(netif_default)));
    return _description;
  }

  // Just the address. "http://192.168.1.42/" is 20 characters and the
  // display has 16.
  const char* describeShort() override {
    return ip4addr_ntoa(netif_ip4_addr(netif_default));
  }

private:
  static const uint32_t CONNECT_TIMEOUT_MS = 20000;  // give up joining after 20 s
  static const int      MAX_CONNECTIONS    = 8;      // two phones loading the page
  static const uint32_t IDLE_CLOSE_MS      = 10000;  // a quiet browser loses its slot
  static const uint32_t STALL_ABORT_MS     = 5000;   // an answer nobody acknowledges
  static const uint32_t REJOIN_AFTER_MS    = 10000;  // link down this long: join again
  static const int      LINES_KEPT         = 16;     // 1.6 s of telemetry at 100 ms
  static const int      COMMANDS_KEPT      = 8;
  static const size_t   HEAD_MAX           = 256;

  bool startListening() {
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;

    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) {
      tcp_close(pcb);
      return false;
    }

    // Several connections at once: a browser opens two or three to the same
    // server, and expects all of them to work.
    _listener = tcp_listen_with_backlog(pcb, MAX_CONNECTIONS);
    if (!_listener) {
      tcp_close(pcb);
      return false;
    }

    for (Connection& c : _connections) c.owner = this;
    // Hand lwIP a pointer to ourselves, so the static callbacks below can
    // find the object they belong to. This is the standard way to bridge
    // from a C library's function pointers back into a C++ object.
    tcp_arg(_listener, this);
    tcp_accept(_listener, onAccept);
    return true;
  }

  // The driver does not join again by itself. After the access point drops
  // the robot (out of range, access point restarted, too many lost frames),
  // the link stays down until a reboot. So: once it has been down for
  // REJOIN_AFTER_MS, start a join, and another one every REJOIN_AFTER_MS
  // until the link is back. The join runs in the background, inside
  // cyw43_arch_poll(); the listener is bound to any address, so it survives
  // and DHCP may hand back a different address.
  void keepLinkUp() {
    bool up = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;

    if (up != _linkUp) {
      _linkUp = up;
      if (up) {
        printf("[wifi] link up: %s\n", describe());
      } else {
        printf("[wifi] link lost\n");
        _linkDownMs = nowMs();
      }
    }
    if (up || nowMs() - _linkDownMs < REJOIN_AFTER_MS) return;

    printf("[wifi] joining %s again (status %d)\n", WIFI_SSID,
           cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA));
    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
    _linkDownMs = nowMs();
  }

  /** One browser connection, and the response it is being sent. */
  struct Connection {
    WebRadio*       owner  = nullptr;
    struct tcp_pcb* pcb    = nullptr;   // nullptr: this slot is free
    HttpRequest     request;
    uint32_t        lastActiveMs = 0;
    bool            keepAlive    = false;
    bool            responding   = false;
    bool            closing      = false;   // tcp_close() failed; poll() retries

    char            head[HEAD_MAX];        // status line and headers
    size_t          headLength = 0, headSent = 0;
    const uint8_t*  body       = nullptr;
    size_t          bodyLength = 0, bodySent = 0;

    // The body of an /api/lines answer is built here, because lwIP may take
    // several round trips to send it and it must still be here when it does.
    char            text[LINES_KEPT * MAX_LINE];
  };

  struct Line {
    uint32_t id = 0;
    char     text[MAX_LINE];
  };

  struct tcp_pcb* _listener = nullptr;
  Connection      _connections[MAX_CONNECTIONS];
  Camera*         _camera = nullptr;
  char            _description[40];

  // What the robot has said, numbered from 1. Line n lives in slot n % LINES_KEPT.
  Line            _lines[LINES_KEPT];
  uint32_t        _lastId = 0;

  // Commands that have arrived and that Robot has not read yet.
  char            _commands[COMMANDS_KEPT][MAX_LINE];
  int             _commandFirst = 0;
  int             _commandCount = 0;

  bool            _heardFrom     = false;
  uint32_t        _lastRequestMs = 0;

  bool            _linkUp        = false;   // as last seen by keepLinkUp()
  uint32_t        _linkDownMs    = 0;       // when it went down, or the last join attempt

  uint32_t        _lastCaptureMs = 0;
  static const uint32_t MIN_CAPTURE_INTERVAL_MS = 1000;

  void handle(Connection& c) {
    _heardFrom     = true;
    _lastRequestMs = nowMs();

    const HttpRequest& r = c.request;
    bool get  = strcmp(r.method(), "GET") == 0;
    bool post = strcmp(r.method(), "POST") == 0;

    if (post && strcmp(r.path(), "/api/command") == 0) return takeCommands(c);
    if (get  && strcmp(r.path(), "/api/lines") == 0)   return sendLines(c);
    if (get  && strcmp(r.path(), "/snapshot.pgm") == 0) return sendSnapshot(c);
    if (get  && sendFile(c)) return;

    respondText(c, "404 Not Found", "nothing here\n");
  }

  // The body may carry several commands, one per line, exactly like a burst
  // typed into `nc`. Each one is queued for Robot::readCommands().
  void takeCommands(Connection& c) {
    char* p = (char*)c.request.body();
    bool  allTaken = true;

    while (*p) {
      char* end = strchr(p, '\n');
      if (end) *end = '\0';

      size_t length = strlen(p);
      if (length && p[length - 1] == '\r') p[--length] = '\0';

      if (length > 0) {
        if (_commandCount < COMMANDS_KEPT) {
          int at = (_commandFirst + _commandCount) % COMMANDS_KEPT;
          strncpy(_commands[at], p, MAX_LINE - 1);
          _commands[at][MAX_LINE - 1] = '\0';
          _commandCount++;
        } else {
          allTaken = false;   // Robot is not reading them: should not happen
        }
      }

      if (!end) break;
      p = end + 1;
    }

    if (allTaken) respond(c, "204 No Content", nullptr, "", nullptr, 0);
    else          respondText(c, "503 Service Unavailable", "too many commands at once\n");
  }

  // Every line with a number after ?after=N. The number of the last one goes
  // in a header, for the page to send back as N next time.
  void sendLines(Connection& c) {
    uint32_t after = (uint32_t)c.request.queryInteger("after", 0);

    // A page that has seen line 900 while we are at line 12 is talking to a
    // robot that restarted since. It gets everything, from the beginning.
    if (after > _lastId) after = 0;

    uint32_t first = after + 1;
    if (_lastId >= LINES_KEPT && first < _lastId - LINES_KEPT + 1) {
      first = _lastId - LINES_KEPT + 1;   // older ones are overwritten already
    }

    size_t used = 0;
    for (uint32_t id = first; id <= _lastId; id++) {
      const char* line = _lines[id % LINES_KEPT].text;
      size_t length = strlen(line);
      if (used + length + 1 > sizeof(c.text)) break;
      memcpy(c.text + used, line, length);
      used += length;
      c.text[used++] = '\n';
    }

    char extra[48];
    snprintf(extra, sizeof(extra), "X-Last-Line: %lu\r\n", (unsigned long)_lastId);
    respond(c, "200 OK", "text/plain; charset=utf-8", extra, (const uint8_t*)c.text, used);
  }

  // The page, and the scripts it loads, from robot/web/. "/" is index.html.
  bool sendFile(Connection& c) {
    const char* path = c.request.path();
    if (strcmp(path, "/") == 0) path = "/index.html";

    for (const WebFile& file : WEB_FILES) {
      if (strcmp(file.path, path) == 0) {
        respond(c, "200 OK", file.type, "", file.data, file.size);
        return true;
      }
    }
    return false;
  }

  // A picture, as a PGM: see Camera.h, and client/remote.js for a reader.
  void sendSnapshot(Connection& c) {
    if (!_camera || !_camera->connected()) {
      respondText(c, "503 Service Unavailable",
                  "No camera. Set CAMERA_ENABLED to true in Config.h, then check the wiring.\n");
      return;
    }

    // A capture blocks the main loop for up to ~30 ms (see Camera.h), and
    // with it the safety check. One picture a second is all a human can use.
    if (nowMs() - _lastCaptureMs < MIN_CAPTURE_INTERVAL_MS) {
      respondText(c, "503 Service Unavailable", "Camera busy, try again in a moment.\n");
      return;
    }
    _lastCaptureMs = nowMs();

    if (!_camera->capture()) {
      respondText(c, "504 Gateway Timeout",
                  "The camera did not send a picture. Check XCLK, PCLK, HREF and VSYNC.\n");
      return;
    }

    // The PGM header ("P5 324 244 255") is part of the image, so it is part
    // of the body. It is short enough to ride in the HTTP header buffer.
    char pgm[32];
    snprintf(pgm, sizeof(pgm), "P5\n%d %d\n255\n", _camera->width(), _camera->height());
    size_t pixels = (size_t)_camera->width() * _camera->height();

    respond(c, "200 OK", "image/x-portable-graymap", "", _camera->data(), pixels, pgm);
  }

  void respondText(Connection& c, const char* status, const char* message) {
    respond(c, status, "text/plain; charset=utf-8", "",
            (const uint8_t*)message, strlen(message));
  }

  // `prefix` is sent after the headers and before `body`, and counted in the
  // Content-Length with it. Only the camera needs it.
  void respond(Connection& c, const char* status, const char* type, const char* extra,
               const uint8_t* body, size_t length, const char* prefix = "") {
    c.keepAlive = c.request.keepAlive();

    int written = snprintf(c.head, sizeof(c.head),
        "HTTP/1.1 %s\r\n"
        "%s%s%s"
        "Content-Length: %u\r\n"
        "Cache-Control: no-store\r\n"     // a reflash must show the new page
        "Connection: %s\r\n"
        "%s"
        "\r\n"
        "%s",
        status,
        type ? "Content-Type: " : "", type ? type : "", type ? "\r\n" : "",
        (unsigned)(strlen(prefix) + length),
        c.keepAlive ? "keep-alive" : "close",
        extra,
        prefix);

    c.headLength = (written > 0 && (size_t)written < sizeof(c.head)) ? (size_t)written
                                                                     : sizeof(c.head) - 1;
    c.headSent   = 0;
    c.body       = body;
    c.bodyLength = length;
    c.bodySent   = 0;
    c.responding = true;

    sendMore(c);
  }

  // Writes as much as lwIP will take, and no more. The page is several
  // kilobytes and lwIP accepts only so much at once, so this is called again
  // from onSent() each time the phone acknowledges some of it. The same
  // shape as sending a camera picture: a big thing through a small pipe.
  void sendMore(Connection& c) {
    if (!c.pcb || !c.responding) return;

    while (c.headSent < c.headLength || c.bodySent < c.bodyLength) {
      u16_t room = tcp_sndbuf(c.pcb);
      if (room == 0) break;               // full for now; onSent() resumes us

      bool           inHead    = c.headSent < c.headLength;
      const uint8_t* from      = inHead ? (const uint8_t*)c.head + c.headSent : c.body + c.bodySent;
      size_t         remaining = inHead ? c.headLength - c.headSent : c.bodyLength - c.bodySent;
      size_t         chunk     = remaining < room ? remaining : room;

      // Out of memory is not an error, it means "later". onSent() or
      // onPoll() will call us again once lwIP has freed some.
      if (tcp_write(c.pcb, from, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) break;

      if (inHead) c.headSent += chunk;
      else        c.bodySent += chunk;
    }

    tcp_output(c.pcb);

    if (c.headSent < c.headLength || c.bodySent < c.bodyLength) return;

    // All queued. Either wait for the next request on this connection, or
    // hang up; tcp_close() still delivers whatever is outstanding first.
    c.responding = false;
    c.request.reset();
    if (!c.keepAlive) closeConnection(c);
  }

  void closeConnection(Connection& c) {
    if (!c.pcb) return;
    tcp_arg(c.pcb, nullptr);
    tcp_recv(c.pcb, nullptr);
    tcp_sent(c.pcb, nullptr);
    tcp_err(c.pcb, nullptr);
    tcp_poll(c.pcb, nullptr, 0);

    // tcp_close() can fail when lwIP is out of memory. Then the connection
    // is still ours, and poll() tries again next time round.
    if (tcp_close(c.pcb) != ERR_OK) {
      tcp_arg(c.pcb, &c);
      tcp_err(c.pcb, onError);
      c.closing = true;
      return;
    }
    c.pcb        = nullptr;
    c.responding = false;
    c.closing    = false;
  }

  // For an answer the phone has stopped acknowledging: it walked out of range
  // or was put away mid-transfer. tcp_close() would keep resending the unsent
  // part for minutes, holding the slot and lwIP memory; tcp_abort() frees both
  // now and sends the phone a reset.
  void abortConnection(Connection& c) {
    if (!c.pcb) return;
    tcp_arg(c.pcb, nullptr);
    tcp_recv(c.pcb, nullptr);
    tcp_sent(c.pcb, nullptr);
    tcp_err(c.pcb, nullptr);
    tcp_poll(c.pcb, nullptr, 0);
    tcp_abort(c.pcb);
    c.pcb        = nullptr;
    c.responding = false;
    c.closing    = false;
  }

  // lwIP requires static callbacks. `arg` is the WebRadio for onAccept() and
  // the Connection for subsequent callbacks (see tcp_arg()).
  static err_t onAccept(void* arg, struct tcp_pcb* pcb, err_t error) {
    WebRadio* self = static_cast<WebRadio*>(arg);
    if (error != ERR_OK || !pcb) return ERR_VAL;

    // A free slot, or else the idle one that has been quiet the longest. A
    // phone that was put away left its connections open; the one in your hand
    // now matters more. A connection halfway through a request or an answer
    // is never taken: cutting off a reply that is already on its way breaks
    // a request that was going to succeed, to make room for one that might.
    Connection* slot = nullptr;
    for (Connection& c : self->_connections) {
      if (!c.pcb) { slot = &c; break; }
      if (c.responding || !c.request.empty()) continue;
      if (!slot || c.lastActiveMs < slot->lastActiveMs) slot = &c;
    }
    if (slot && slot->pcb) self->closeConnection(*slot);
    if (!slot || slot->pcb) {             // all busy, or could not close: refuse
      tcp_abort(pcb);
      return ERR_ABRT;
    }

    slot->pcb          = pcb;
    slot->lastActiveMs = nowMs();
    slot->responding   = false;
    slot->closing      = false;
    slot->request.reset();

    tcp_arg(pcb, slot);
    tcp_recv(pcb, onReceive);
    tcp_sent(pcb, onSent);
    tcp_err(pcb, onError);
    tcp_poll(pcb, onPoll, 2);             // every second or so, see onPoll()
    tcp_nagle_disable(pcb);               // small answers go out at once
    return ERR_OK;
  }

  static err_t onReceive(void* arg, struct tcp_pcb* pcb, struct pbuf* buffer, err_t error) {
    Connection* c = static_cast<Connection*>(arg);

    if (!buffer || error != ERR_OK) {     // the browser hung up
      if (buffer) pbuf_free(buffer);
      if (c) c->owner->closeConnection(*c);
      return ERR_OK;
    }

    tcp_recved(pcb, buffer->tot_len);
    if (!c) { pbuf_free(buffer); return ERR_OK; }
    c->lastActiveMs = nowMs();

    HttpRequest::State state = HttpRequest::INCOMPLETE;
    for (struct pbuf* chunk = buffer; chunk != nullptr; chunk = chunk->next) {
      state = c->request.feed((const char*)chunk->payload, chunk->len);
    }
    pbuf_free(buffer);

    // A browser does not send its next request before the answer to this
    // one, so a request arriving mid-answer is not one to act on.
    if (c->responding) return ERR_OK;

    if (state == HttpRequest::COMPLETE) {
      c->owner->handle(*c);
    } else if (state == HttpRequest::BAD) {
      c->keepAlive = false;
      c->owner->respondText(*c, "400 Bad Request", "that was not a request I understand\n");
    }
    return ERR_OK;
  }

  // Acknowledged bytes free lwIP memory, which ANY connection may have been
  // waiting for: the page and its two scripts are asked for at the same
  // moment, and together they are more than lwIP holds. So every response
  // still going gets another try, not just this one.
  static err_t onSent(void* arg, struct tcp_pcb* pcb, u16_t length) {
    (void)pcb; (void)length;
    Connection* c = static_cast<Connection*>(arg);
    if (!c) return ERR_OK;
    c->lastActiveMs = nowMs();
    for (Connection& other : c->owner->_connections) c->owner->sendMore(other);
    return ERR_OK;
  }

  // lwIP calls this every second or so on each open connection. It catches
  // the one case onSent() cannot: a tcp_write() that failed for lack of
  // memory while nothing at all was in flight, so no acknowledgement is
  // coming to wake us up.
  static err_t onPoll(void* arg, struct tcp_pcb* pcb) {
    (void)pcb;
    Connection* c = static_cast<Connection*>(arg);
    if (c) c->owner->sendMore(*c);
    return ERR_OK;
  }

  // The connection broke and lwIP has already freed it: forget it, do not
  // close it.
  static void onError(void* arg, err_t error) {
    (void)error;
    Connection* c = static_cast<Connection*>(arg);
    if (!c) return;
    c->pcb        = nullptr;
    c->responding = false;
    c->closing    = false;
  }
};
