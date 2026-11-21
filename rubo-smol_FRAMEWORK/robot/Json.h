#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>

// JSON reader/writer for the robot's flat-object protocol. Supported values:
// integers, booleans, null and strings.
//
//      {"left": 200, "right": -200, "stop": false, "seq": 7}
//
// Nested objects, arrays and fractional numbers are rejected. Numbers are
// returned as long; use integral units such as milliseconds in the protocol.


/**
 * Turns a line of text into fields you can ask about.
 *
 *     JsonReader command;
 *     if (!command.parse(line)) { ...it was not valid... }
 *     if (command.has("left")) int speed = command.integer("left");
 */
class JsonReader {
public:
  static const int MAX_FIELDS = 12;   // more than any command needs
  static const int MAX_KEY    = 16;   // "telemetry" is the longest we use

  // Returns false if the text is not a flat object of the kind described
  // above. On failure nothing is readable: every way out of this function
  // goes through fail(), which throws away the fields collected so far. A
  // half-parsed command is worse than no command -- it is a command with
  // some of the numbers missing, and it would still move the wheels.
  bool parse(const char* text) {
    _count = 0;
    const char* p = text;

    p = skipSpaces(p);
    if (*p != '{') return fail();
    p++;

    p = skipSpaces(p);
    if (*p == '}') return true;        // {} is valid, and simply has no fields

    for (;;) {
      p = skipSpaces(p);
      if (*p != '"') return fail();     // keys must be quoted, as JSON requires
      p++;

      // Copy the key, stopping if it is longer than we have room for.
      char key[MAX_KEY];
      int  keyLength = 0;
      while (*p && *p != '"') {
        if (keyLength < MAX_KEY - 1) key[keyLength++] = *p;
        p++;
      }
      if (*p != '"') return fail();     // ran off the end of the line
      key[keyLength] = '\0';
      p++;

      p = skipSpaces(p);
      if (*p != ':') return fail();
      p++;
      p = skipSpaces(p);

      bool store = true;
      long value = 0;

      if (*p == '-' || (*p >= '0' && *p <= '9')) {
        char* end = nullptr;
        value = strtol(p, &end, 10);
        if (end == p) return fail();
        if (*end == '.') return fail(); // a fraction: refuse rather than truncate
        p = end;
      } else if (matches(p, "true")) {
        value = 1; p += 4;
      } else if (matches(p, "false")) {
        value = 0; p += 5;
      } else if (matches(p, "null")) {
        store = false;                 // null is treated as "not sent at all"
        p += 4;
      } else if (*p == '"') {
        // A string value. We record that the key was present but keep no
        // text: no command needs one. Skipping \" keeps an escaped quote
        // from ending the string early.
        p++;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
        if (*p != '"') return fail();
        p++;
      } else {
        return fail();                  // {, [, or junk
      }

      if (store) {
        Field* existing = find(key);
        if (existing) {
          // duplicate key: the last one wins, as in JSON
          existing->value = value;
        } else if (_count < MAX_FIELDS) {
          strcpy(_fields[_count].key, key);
          _fields[_count].value = value;
          _count++;
        }
      }

      p = skipSpaces(p);
      if (*p == ',') { p++; continue; }
      if (*p == '}') return true;
      return fail();
    }
  }

  // Was this key present? Note that a key sent as null counts as absent,
  // which is what lets {"duration": null} mean "use the default".
  bool has(const char* key) const { return find(key) != nullptr; }

  long integer(const char* key, long fallback = 0) const {
    const Field* f = find(key);
    return f ? f->value : fallback;
  }

  bool boolean(const char* key, bool fallback = false) const {
    const Field* f = find(key);
    return f ? (f->value != 0) : fallback;
  }

private:
  // Every failed parse leaves the reader empty rather than half-filled.
  // Returning it directly (`return fail();`) keeps each check to one line.
  bool fail() { _count = 0; return false; }

  struct Field {
    char key[MAX_KEY];
    long value;
  };

  Field _fields[MAX_FIELDS];
  int   _count = 0;

  const Field* find(const char* key) const {
    for (int i = 0; i < _count; i++) {
      if (strcmp(_fields[i].key, key) == 0) return &_fields[i];
    }
    return nullptr;
  }

  // The same search, writable: parse() uses it to update a repeated key in
  // place rather than adding a second field for it.
  Field* find(const char* key) {
    for (int i = 0; i < _count; i++) {
      if (strcmp(_fields[i].key, key) == 0) return &_fields[i];
    }
    return nullptr;
  }

  static const char* skipSpaces(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
  }

  static bool matches(const char* p, const char* word) {
    return strncmp(p, word, strlen(word)) == 0;
  }
};


/**
 * Builds a message into a buffer you provide.
 *
 *     char line[256];
 *     JsonWriter out(line, sizeof(line));
 *     out.integer("ms", 41230).boolean("moving", true);
 *     radio.send(out.finish());
 *
 * It writes into memory you already own, so nothing is allocated while the
 * robot is running. Running out of room is not a crash: the writer stops
 * and overflowed() says so.
 */
class JsonWriter {
public:
  JsonWriter(char* buffer, size_t size) : _buffer(buffer), _size(size) {
    // A zero-size buffer has no room even for the opening brace.
    if (_size > 0) {
      _buffer[0] = '{';
      _used      = 1;
    }
  }

  JsonWriter& integer(const char* key, long value) {
    separate();
    append("\"%s\":%ld", key, value);
    return *this;
  }

  JsonWriter& boolean(const char* key, bool value) {
    separate();
    append("\"%s\":%s", key, value ? "true" : "false");
    return *this;
  }

  // For short, plain text only -- quotes and backslashes are not escaped,
  // so do not pass anything that came in from the network.
  JsonWriter& text(const char* key, const char* value) {
    separate();
    append("\"%s\":\"%s\"", key, value);
    return *this;
  }

  // Closes the object and returns the finished line, ready to send.
  const char* finish() {
    if (_used + 2 <= _size) {
      _buffer[_used++] = '}';
      _buffer[_used]   = '\0';
    } else {
      _overflowed = true;
      if (_size > 0) _buffer[_size - 1] = '\0';   // a zero-size buffer has no last byte to hold it in
    }
    return _buffer;
  }

  bool overflowed() const { return _overflowed; }

private:
  char*  _buffer;
  size_t _size;
  size_t _used       = 0;
  bool   _first      = true;
  bool   _overflowed = false;

  void separate() {
    if (_first) { _first = false; return; }
    if (_used + 1 < _size) _buffer[_used++] = ',';
    else _overflowed = true;
  }

  // snprintf never writes past the end, and tells us how much it wanted to
  // write -- which is how we detect that the message did not fit.
  void append(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    if (_used >= _size) { _overflowed = true; return; }

    va_list args;
    va_start(args, format);
    int wanted = vsnprintf(_buffer + _used, _size - _used, format, args);
    va_end(args);

    if (wanted < 0 || (size_t)wanted >= _size - _used) {
      _overflowed = true;
      _used = _size - 1;
    } else {
      _used += wanted;
    }
  }
};
