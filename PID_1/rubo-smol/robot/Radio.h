#pragma once
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "Config.h"

/**
 * Transport interface for newline-terminated JSON messages. Robot.h uses
 * Radio without knowing whether the link is USB serial or HTTP. SerialRadio
 * is below; WebRadio is in WebRadio.h.
 */
class Radio {
public:
  // Maximum line length including the terminating '\0'.
  static constexpr size_t MAX_LINE = 256;

  // Allow derived radios to be destroyed through a Radio pointer.
  virtual ~Radio() {}

  // Bring the link up. Returns false if it could not be established.
  virtual bool begin() = 0;

  // Called on every pass through the main loop.
  virtual void poll() = 0;

  // Robot.h stops the wheels when this becomes false.
  virtual bool connected() = 0;

  // Copy a complete line if available; never block waiting for input.
  virtual bool receive(char* line, size_t maxLength) = 0;

  // Send one line. Silently does nothing if nobody is connected.
  virtual void send(const char* line) = 0;

  // Startup description, such as the IP address.
  virtual const char* describe() = 0;

  // Short description for the 16-column display.
  virtual const char* describeShort() { return describe(); }
};


/**
 * USB serial transport. printf output shares the port with JSON telemetry;
 * clients ignore lines that do not start with '{'.
 */
class SerialRadio : public Radio {
public:
  // main.cpp initializes USB stdio before calling begin().
  bool begin() override { return true; }

  // The SDK buffers USB input without explicit polling.
  void poll() override {}

  // Closing the host's serial port makes Robot::enforceSafety() stop movement.
  bool connected() override { return stdio_usb_connected(); }

  bool receive(char* line, size_t maxLength) override {
    // Drop a partial command when the host closes the port.
    if (!connected()) {
      _length = 0;
      return false;
    }

    for (;;) {
      // Zero timeout keeps receive() non-blocking.
      int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) return false;

      if (c == '\r') continue;          // Windows terminals send \r\n

      if (c == '\n') {                  // a complete line
        size_t length = _length < maxLength - 1 ? _length : maxLength - 1;
        memcpy(line, _buffer, length);
        line[length] = '\0';
        _length = 0;
        return length > 0;              // a blank line is not a command
      }

      // Drop bytes beyond the maximum line length.
      if (_length < sizeof(_buffer)) _buffer[_length++] = (char)c;
    }
  }

  void send(const char* line) override { printf("%s\n", line); }

  const char* describe() override      { return "USB serial at 115200 baud"; }
  const char* describeShort() override { return "USB cable"; }

private:
  char   _buffer[MAX_LINE];
  size_t _length = 0;
};
