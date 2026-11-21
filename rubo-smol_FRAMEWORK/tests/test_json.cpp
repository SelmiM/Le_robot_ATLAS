// Host-side tests for Json.h. Run with python3 tools/test.py.
// Add cases when extending the parser.

#include "../robot/Json.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while(0)

int main() {
  JsonReader r;

  // the real commands
  CHECK(r.parse("{\"left\": 200, \"right\": -200}"));
  CHECK(r.integer("left") == 200);
  CHECK(r.integer("right") == -200);
  CHECK(!r.has("duration"));

  CHECK(r.parse("{\"stop\": true}"));
  CHECK(r.boolean("stop") == true);
  CHECK(r.parse("{\"stop\": false}"));
  CHECK(r.boolean("stop") == false);
  CHECK(r.has("stop"));

  CHECK(r.parse("{\"seq\":7,\"telemetry\":500,\"left\":100,\"right\":100,\"duration\":3000}"));
  CHECK(r.integer("seq") == 7 && r.integer("telemetry") == 500 && r.integer("duration") == 3000);

  // null means absent
  CHECK(r.parse("{\"duration\": null, \"left\": 5}"));
  CHECK(!r.has("duration"));
  CHECK(r.integer("duration", 999) == 999);
  CHECK(r.integer("left") == 5);

  // whitespace, empty object
  CHECK(r.parse("  {  \"a\" :  1 ,  \"b\" : 2  }  "));
  CHECK(r.integer("a")==1 && r.integer("b")==2);
  CHECK(r.parse("{}"));
  CHECK(!r.has("a"));

  // string value: key present, no crash
  CHECK(r.parse("{\"name\":\"bob\",\"x\":3}"));
  CHECK(r.integer("x") == 3);

  // escaped quote inside a string must not end it early
  CHECK(r.parse("{\"s\":\"a\\\"b\",\"x\":4}"));
  CHECK(r.integer("x") == 4);

  // duplicate keys: the last one wins, as in JSON
  CHECK(r.parse("{\"left\": 100, \"left\": 200}"));
  CHECK(r.integer("left") == 200);

  // rejections
  CHECK(!r.parse("not json"));
  CHECK(!r.parse("{\"a\":1"));            // truncated
  CHECK(!r.parse("{\"a\":}"));            // no value
  CHECK(!r.parse("{a:1}"));               // unquoted key
  CHECK(!r.parse("{\"a\":1.5}"));         // fraction refused, not truncated
  CHECK(!r.parse("{\"a\":[1,2]}"));       // array
  CHECK(!r.parse("{\"a\":{\"b\":1}}"));   // nesting
  CHECK(!r.parse("{\"a\":1 \"b\":2}"));   // missing comma
  CHECK(!r.parse("{\"a\":\"unterminated"));

  // a FAILED parse must leave nothing readable behind, or a rejected command
  // would still be half-answerable -- see fail() in Json.h
  CHECK(r.parse("{\"left\":200,\"right\":200}"));
  CHECK(r.has("left"));
  CHECK(!r.parse("{\"left\":200,\"right\":}"));   // valid prefix, junk after
  CHECK(!r.has("left"));                          // ...and "left" is gone with it
  CHECK(r.integer("left", 42) == 42);

  // over-long key is truncated, not overflowed
  CHECK(r.parse("{\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":1}"));

  // more fields than MAX_FIELDS: extras dropped, no overflow
  CHECK(r.parse("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,"
                "\"i\":9,\"j\":10,\"k\":11,\"l\":12,\"m\":13,\"n\":14}"));
  CHECK(r.integer("a") == 1);

  // Writer cases.
  char buf[256];
  { JsonWriter w(buf, sizeof(buf));
    w.integer("seq", 7).integer("ms", 41230).boolean("moving", true);
    CHECK(strcmp(w.finish(), "{\"seq\":7,\"ms\":41230,\"moving\":true}") == 0);
    CHECK(!w.overflowed()); }

  { JsonWriter w(buf, sizeof(buf));
    CHECK(strcmp(w.finish(), "{}") == 0); }

  { JsonWriter w(buf, sizeof(buf));
    w.text("error", "that was not valid JSON");
    CHECK(strcmp(w.finish(), "{\"error\":\"that was not valid JSON\"}") == 0); }

  // overflow must be reported, not written past the end
  { char small[16];
    JsonWriter w(small, sizeof(small));
    for (int i = 0; i < 20; i++) w.integer("key", 123456);
    w.finish();
    CHECK(w.overflowed());
    CHECK(strlen(small) < sizeof(small)); }

  // A WHOLE TELEMETRY LINE MUST FIT IN Radio::MAX_LINE, with every number at
  // its longest. If you add a field to sendTelemetry(), add it here too: an
  // overflowing line is not a crash, it is silently truncated JSON that the
  // client then fails to parse, which is much harder to work out from the
  // other end.
  { JsonWriter w(buf, sizeof(buf));
    w.integer("seq", -2147483647L - 1).integer("ms", 4294967295L)
     .integer("left", -255).integer("right", -255).boolean("moving", true)
     .integer("mission", -2147483647L - 1)
     .integer("front_mm", -1).boolean("front_ok", false).integer("front_q", -1)
     .integer("side_mm", -1).boolean("side_ok", false).integer("side_q", -1)
     .boolean("done", true);
    const char* line = w.finish();
    CHECK(!w.overflowed());
    CHECK(strlen(line) < 256);          // Radio::MAX_LINE
    JsonReader back;
    CHECK(back.parse(line));            // and it is still readable JSON
    CHECK(back.integer("side_q") == -1); }

  // round trip: what the robot sends, the robot could read back
  { JsonWriter w(buf, sizeof(buf));
    w.integer("front_mm", -1).boolean("front_ok", false);
    JsonReader back;
    CHECK(back.parse(w.finish()));
    CHECK(back.integer("front_mm") == -1);
    CHECK(back.boolean("front_ok") == false); }

  printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nall checks passed\n", fails);
  return fails != 0;
}
