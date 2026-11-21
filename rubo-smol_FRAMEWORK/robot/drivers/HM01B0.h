#pragma once
#include <cstdint>
#include <cstddef>
#include "pico/stdlib.h"

// Low-level camera driver; use Camera.h for the robot-facing API.
// Based on ArduCAM's Pico driver:
// https://github.com/ArduCAM/RPI-Pico-Cam (rp2040_hm01b0/arducam).
// Adapted to C++ with configurable pins, pre-assembled PIO instructions,
// and dynamic PIO/DMA allocation to avoid conflicts with the WiFi chip.

// The sensor's native output. Not adjustable without new register tables.
static const int HM01B0_WIDTH  = 324;
static const int HM01B0_HEIGHT = 244;

/** Camera control and data pins for the HM01B0 driver. */
struct HM01B0Pins {
  uint resetb;  // camera reset
  uint xclk;    // clock we generate for the camera
  uint siod;    // I2C data  (bit-banged, NOT one of the i2c controllers)
  uint sioc;    // I2C clock (bit-banged)
  uint d0;      // pixel data. PCLK must be d0+8 and HREF must be d0+9.
  uint vsync;   // start-of-frame
};

/** Low-level HM01B0 camera driver using PIO and DMA to capture frames. */
class HM01B0 {
public:
  // Returns false if no PIO state machine or DMA channel could be claimed.
  bool begin(const HM01B0Pins& pins);

  // Fills buffer with one frame, one byte of grey per pixel.
  // buffer must hold at least HM01B0_WIDTH * HM01B0_HEIGHT bytes.
  // Blocks for roughly one frame period (~30 ms), and gives up after
  // CAPTURE_TIMEOUT_MS if the camera is not producing pixels.
  bool captureFrame(uint8_t* buffer, size_t bufferSize);

private:
  // A camera that is not clocking must never freeze the robot: the motors
  // could still be turning, so every wait in the driver is bounded by this.
  // A healthy camera delivers a frame in ~30 ms, so this is the give-up time
  // for a dead one.
  static const unsigned long CAPTURE_TIMEOUT_MS = 100;

  HM01B0Pins _pins{};
  uint16_t   _programWords[4];   // the PIO program, re-pointed at this wiring's pins
  void*      _pio        = nullptr;   // PIO instance, opaque here
  uint       _sm         = 0;
  int        _dmaChannel = -1;
  bool       _ready      = false;

  void writeRegister(uint16_t reg, uint8_t value);
  bool waitForVsync(bool level);
  void resetStateMachine(void* pioInstance);
};
