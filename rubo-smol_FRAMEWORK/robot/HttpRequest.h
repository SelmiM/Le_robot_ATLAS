#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <strings.h>   // strncasecmp

/**
 * Collect one HTTP request from a TCP byte stream, which may split a request
 * across any number of reads. For example:
 *
 *     POST /api/command HTTP/1.1\r\n
 *     Host: 192.168.1.42\r\n
 *     Content-Length: 29\r\n
 *     \r\n
 *     {"left":200,"right":200}
 *
 * feed() reports whether all headers and Content-Length body bytes have arrived:
 *
 *     HttpRequest request;
 *     switch (request.feed(bytes, count)) {
 *       case HttpRequest::INCOMPLETE: ...wait for more...
 *       case HttpRequest::COMPLETE:   ...request.path(), request.body()...
 *       case HttpRequest::BAD:        ...answer 400 and hang up...
 *     }
 *
 * Host-side tests are in tests/test_http.cpp.
 */
class HttpRequest {
public:
  // Room for the request line, the headers and the body together. A phone's
  // browser sends 400-700 bytes of headers; a robot command is under 100.
  static const size_t MAX_SIZE = 1536;

  enum State { INCOMPLETE, COMPLETE, BAD };

  void reset() {
    _used        = 0;
    _state       = INCOMPLETE;
    _bodyAt      = 0;
    _bodyLength  = 0;
    _method = _path = _query = "";
    _keepAlive   = true;
  }

  HttpRequest() { reset(); }

  // Appends `count` bytes and reports where the request stands. Once it has
  // said COMPLETE or BAD, further bytes are ignored until reset().
  State feed(const char* bytes, size_t count) {
    if (_state != INCOMPLETE) return _state;

    if (count > MAX_SIZE - _used) return _state = BAD;   // too big for us
    memcpy(_buffer + _used, bytes, count);
    _used += count;
    _buffer[_used] = '\0';

    // Still waiting for the blank line that ends the headers?
    if (_bodyAt == 0) {
      const char* end = strstr(_buffer, "\r\n\r\n");
      if (!end) return _state;
      _bodyAt = (size_t)(end - _buffer) + 4;
      if (!parseHeaders()) return _state = BAD;
    }

    // Headers done; is all of the body here?
    if (_bodyAt + _bodyLength > MAX_SIZE) return _state = BAD;
    if (_used < _bodyAt + _bodyLength) return _state;

    // Anything past the body would be a second request sent without waiting
    // for the first answer ("pipelining"). Browsers do not do that, so it is
    // cut off rather than kept.
    _buffer[_bodyAt + _bodyLength] = '\0';
    return _state = COMPLETE;
  }

  // Nothing received since reset(): the connection is between requests.
  bool empty() const { return _used == 0; }

  const char* method() const { return _method; }   // "GET", "POST"
  const char* path()   const { return _path; }     // "/api/lines"
  const char* query()  const { return _query; }    // "after=12", or ""
  const char* body()   const { return _buffer + _bodyAt; }
  size_t      bodyLength() const { return _bodyLength; }

  // Whether the browser wants to send its next request down this same
  // connection. HTTP/1.1 does unless it says "Connection: close".
  bool keepAlive() const { return _keepAlive; }

  // A whole number from the query string: for "after=12&x=3",
  // queryInteger("after") is 12. `fallback` when the name is not there.
  long queryInteger(const char* name, long fallback = 0) const {
    size_t length = strlen(name);
    const char* p = _query;
    while (*p) {
      if (strncmp(p, name, length) == 0 && p[length] == '=') {
        return strtol(p + length + 1, nullptr, 10);
      }
      p = strchr(p, '&');
      if (!p) break;
      p++;
    }
    return fallback;
  }

private:
  char        _buffer[MAX_SIZE + 1];   // +1 for the terminating '\0'
  size_t      _used;
  State       _state;
  size_t      _bodyAt;       // where the body starts; 0 until the headers are in
  size_t      _bodyLength;
  const char* _method;       // these three point into _buffer
  const char* _path;
  const char* _query;
  bool        _keepAlive;

  // Splits the request line in place, by writing '\0' over the spaces, and
  // reads the two headers we care about. Returns false if it is not HTTP.
  bool parseHeaders() {
    char* line    = _buffer;
    char* lineEnd = strstr(line, "\r\n");
    *lineEnd = '\0';

    // "POST /api/command?x=1 HTTP/1.1"
    char* space1 = strchr(line, ' ');
    if (!space1) return false;
    char* space2 = strchr(space1 + 1, ' ');
    if (!space2) return false;
    *space1 = '\0';
    *space2 = '\0';

    _method = line;
    _path   = space1 + 1;
    if (_path[0] != '/') return false;

    char* question = strchr(space1 + 1, '?');
    if (question) {
      *question = '\0';
      _query = question + 1;
    }

    const char* version = space2 + 1;
    if (strncmp(version, "HTTP/1.", 7) != 0) return false;
    _keepAlive = strcmp(version, "HTTP/1.0") != 0;   // 1.0 closes by default

    // The header lines, one at a time, up to the blank line.
    char* headerEnd = _buffer + _bodyAt - 4;
    for (char* header = lineEnd + 2; header < headerEnd; ) {
      char* end = strstr(header, "\r\n");
      *end = '\0';

      if (named(header, "Content-Length")) {
        long length = strtol(valueOf(header), nullptr, 10);
        if (length < 0 || length > (long)MAX_SIZE) return false;
        _bodyLength = (size_t)length;
      } else if (named(header, "Connection")) {
        const char* value = valueOf(header);
        if (strncasecmp(value, "close", 5) == 0)      _keepAlive = false;
        if (strncasecmp(value, "keep-alive", 10) == 0) _keepAlive = true;
      }
      header = end + 2;
    }
    return true;
  }

  // Header names are not case sensitive: "content-length" is the same header.
  static bool named(const char* header, const char* name) {
    size_t length = strlen(name);
    return strncasecmp(header, name, length) == 0 && header[length] == ':';
  }

  static const char* valueOf(const char* header) {
    const char* p = strchr(header, ':') + 1;
    while (*p == ' ' || *p == '\t') p++;
    return p;
  }
};
