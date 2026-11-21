// Host-side tests for HttpRequest.h. Run with python3 tools/test.py.
// Cover splits within headers, before the body, and one byte at a time.

#include "../robot/HttpRequest.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while(0)

static HttpRequest::State feedAll(HttpRequest& r, const char* text) {
  return r.feed(text, strlen(text));
}

int main() {
  const char* post =
    "POST /api/command HTTP/1.1\r\n"
    "Host: 192.168.1.42\r\n"
    "content-length: 24\r\n"
    "\r\n"
    "{\"left\":200,\"right\":200}";

  // in one piece
  { HttpRequest r;
    CHECK(feedAll(r, post) == HttpRequest::COMPLETE);
    CHECK(strcmp(r.method(), "POST") == 0);
    CHECK(strcmp(r.path(), "/api/command") == 0);
    CHECK(strcmp(r.query(), "") == 0);
    CHECK(r.bodyLength() == 24);
    CHECK(strcmp(r.body(), "{\"left\":200,\"right\":200}") == 0);
    CHECK(r.keepAlive()); }

  // cut between the headers and the body, the split a phone really makes
  { HttpRequest r;
    const char* body = strstr(post, "\r\n\r\n") + 4;
    CHECK(r.feed(post, body - post) == HttpRequest::INCOMPLETE);
    CHECK(feedAll(r, body) == HttpRequest::COMPLETE);
    CHECK(strcmp(r.body(), "{\"left\":200,\"right\":200}") == 0); }

  // one byte at a time: complete exactly on the last byte, and not before
  { HttpRequest r;
    size_t length = strlen(post);
    for (size_t i = 0; i + 1 < length; i++)
      CHECK(r.feed(post + i, 1) == HttpRequest::INCOMPLETE);
    CHECK(r.feed(post + length - 1, 1) == HttpRequest::COMPLETE);
    CHECK(strcmp(r.path(), "/api/command") == 0);
    CHECK(r.bodyLength() == 24); }

  // a GET with a query string and no body
  { HttpRequest r;
    CHECK(feedAll(r, "GET /api/lines?after=17&x=3 HTTP/1.1\r\nHost: r\r\n\r\n")
          == HttpRequest::COMPLETE);
    CHECK(strcmp(r.method(), "GET") == 0);
    CHECK(strcmp(r.path(), "/api/lines") == 0);
    CHECK(r.queryInteger("after") == 17);
    CHECK(r.queryInteger("x") == 3);
    CHECK(r.queryInteger("missing", -5) == -5);
    CHECK(r.queryInteger("afte", -5) == -5);   // a prefix is not the name
    CHECK(r.bodyLength() == 0); }

  // no headers at all
  { HttpRequest r;
    CHECK(feedAll(r, "GET / HTTP/1.1\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(strcmp(r.path(), "/") == 0); }

  // keep-alive: 1.1 by default, 1.0 not, and the header overrides both
  { HttpRequest r;
    CHECK(feedAll(r, "GET / HTTP/1.1\r\nConnection: close\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(!r.keepAlive());
    r.reset();
    CHECK(feedAll(r, "GET / HTTP/1.0\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(!r.keepAlive());
    r.reset();
    CHECK(feedAll(r, "GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(r.keepAlive()); }

  // reset() makes it ready for the next request on the same connection
  { HttpRequest r;
    CHECK(r.empty());
    CHECK(feedAll(r, post) == HttpRequest::COMPLETE);
    CHECK(!r.empty());
    r.reset();
    CHECK(r.empty());
    CHECK(feedAll(r, "GET /robot.js HTTP/1.1\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(strcmp(r.path(), "/robot.js") == 0);
    CHECK(r.bodyLength() == 0); }

  // bytes after the body are not part of this request
  { HttpRequest r;
    CHECK(feedAll(r, "POST /a HTTP/1.1\r\nContent-Length: 2\r\n\r\nokEXTRA") == HttpRequest::COMPLETE);
    CHECK(strcmp(r.body(), "ok") == 0); }

  // once complete, more bytes change nothing
  { HttpRequest r;
    CHECK(feedAll(r, "GET /x HTTP/1.1\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(feedAll(r, "GET /y HTTP/1.1\r\n\r\n") == HttpRequest::COMPLETE);
    CHECK(strcmp(r.path(), "/x") == 0); }

  // things that are not HTTP, or that we will not hold
  { HttpRequest r;
    CHECK(feedAll(r, "hello\r\n\r\n") == HttpRequest::BAD);
    r.reset();
    CHECK(feedAll(r, "GET index.html HTTP/1.1\r\n\r\n") == HttpRequest::BAD);   // no leading /
    r.reset();
    CHECK(feedAll(r, "GET / SPDY/3\r\n\r\n") == HttpRequest::BAD);
    r.reset();
    CHECK(feedAll(r, "POST / HTTP/1.1\r\nContent-Length: -4\r\n\r\n") == HttpRequest::BAD);
    r.reset();
    CHECK(feedAll(r, "POST / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n") == HttpRequest::BAD);
    r.reset();
    CHECK(feedAll(r, "POST / HTTP/1.1\r\nContent-Length: 1500\r\n\r\n") == HttpRequest::BAD);   // fits alone, not with the headers
  }

  // a request that never ends is refused once it no longer fits
  { HttpRequest r;
    char junk[256];
    memset(junk, 'a', sizeof(junk));
    HttpRequest::State state = HttpRequest::INCOMPLETE;
    for (int i = 0; i < 10 && state == HttpRequest::INCOMPLETE; i++)
      state = r.feed(junk, sizeof(junk));
    CHECK(state == HttpRequest::BAD); }

  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nall checks passed\n", fails);
  return fails != 0;
}
