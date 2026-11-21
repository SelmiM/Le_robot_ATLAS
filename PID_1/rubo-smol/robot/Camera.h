#pragma once
#include <cstdint>
#include <cstddef>
#include "Config.h"
#include "drivers/HM01B0.h"

/**
 * Optional HM01B0 camera. Enable it in Config.h to capture greyscale frames.
 *
 *     camera.capture();                  // grab one picture
 *     int grey = camera.pixel(10, 20);   // 0 = black, 255 = white
 *     int avg  = camera.brightness();    // average over the whole picture
 *
 * A 324 x 244 frame uses 79,056 bytes; capture() replaces the previous frame.
 * WebRadio.h serves the frame at /snapshot.pgm. Chip I/O is in drivers/HM01B0.cpp.
 */
class Camera {
public:
  // Returns false if the camera is switched off in Config.h, or did not
  // respond. Safe to call either way.
  bool begin() {
    if (!CAMERA_ENABLED) return false;

    HM01B0Pins pins;
    pins.resetb = CAMERA_RESET_PIN;
    pins.xclk   = CAMERA_XCLK_PIN;
    pins.siod   = CAMERA_SDA_PIN;
    pins.sioc   = CAMERA_SCL_PIN;
    pins.d0     = CAMERA_D0_PIN;
    pins.vsync  = CAMERA_VSYNC_PIN;

    _ready = _driver.begin(pins);
    return _ready;
  }

  bool connected() const { return _ready; }

  // Capture blocks the main loop for about 30 ms. Use it on demand, not on
  // every pass; the safety check cannot run until it returns.
  bool capture() {
    if (!_ready) return false;
    if (!_driver.captureFrame(_buffer, sizeof(_buffer))) return false;
    _hasPicture = true;
    return true;
  }

  bool hasPicture() const { return _hasPicture; }

  int width()  const { return HM01B0_WIDTH; }
  int height() const { return HM01B0_HEIGHT; }

  // Grey level of one pixel, 0 (black) to 255 (white). Returns -1 if there is
  // no picture yet, or if x,y is outside it. x runs left to right, y runs
  // top to bottom, both starting at 0.
  int pixel(int x, int y) const {
    if (!_hasPicture) return -1;
    if (x < 0 || x >= HM01B0_WIDTH || y < 0 || y >= HM01B0_HEIGHT) return -1;
    return _buffer[y * HM01B0_WIDTH + x];
  }

  // Average grey level of the whole picture. Handy for "is the light on?",
  // and the simplest possible thing to do with 79,000 numbers.
  int brightness() const {
    if (!_hasPicture) return -1;
    uint32_t total = 0;   // 32-bit: 79,056 pixels x 255 overflows a 16-bit int
    for (size_t i = 0; i < sizeof(_buffer); i++) total += _buffer[i];
    return (int)(total / sizeof(_buffer));
  }

  // Raw pixels, for sending over the network. Row by row, top row first.
  const uint8_t* data() const { return _buffer; }
  size_t size()     const { return sizeof(_buffer); }

private:
#if CAMERA_ENABLED
  static const size_t FRAME_BYTES = (size_t)HM01B0_WIDTH * HM01B0_HEIGHT;
#else
  // The camera is off, so do not reserve 79 KB for a picture that will never
  // be taken. Every method above refuses to touch the buffer unless _ready or
  // _hasPicture is set, and neither can become true while the camera is off.
  static const size_t FRAME_BYTES = 1;
#endif

  HM01B0  _driver;
  bool    _ready      = false;
  bool    _hasPicture = false;
  uint8_t _buffer[FRAME_BYTES];
};
