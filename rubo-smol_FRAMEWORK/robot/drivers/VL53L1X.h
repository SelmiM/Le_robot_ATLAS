#pragma once
#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Low-level VL53L1X driver; use TofSensor.h for the robot-facing API.
// ST does not publish a register map; the 91-byte startup configuration comes
// from its Ultra Lite Driver (attributed in VL53L1X.cpp). Timed transfers
// set lost() on failure so a disconnected sensor cannot block the main loop.

/** Low-level VL53L1X time-of-flight sensor driver with timed I2C transfers. */
class VL53L1X {
public:
  // Fixed at power-up. It can be changed in software, but not kept: it comes
  // back as 0x29 after every power cycle.
  static const uint8_t ADDRESS = 0x29;

  // How far the sensor tries to see. Short reaches about 1.3 m and copes far
  // better with bright light; Long reaches about 4 m in the dark.
  enum class Mode { Short, Long };

  // Checks the chip's ID, waits for its firmware to boot, writes ST's
  // configuration block and lets it calibrate itself on one throwaway
  // measurement. Blocks for up to about 120 ms, never more than ~500.
  // False if nothing answered at 0x29, or if it is not a VL53L1X.
  bool begin(i2c_inst_t* bus);

  // Laser timing for the mode, with a 33 ms timing budget.
  void setMode(Mode mode);

  // How often a new measurement starts. At least the 33 ms budget; ST's
  // configuration block leaves it at 100 ms.
  void setIntervalMs(uint32_t ms);

  // Start measuring continuously, and never stop. From then on the sensor
  // works on its own and dataReady() says when there is something new.
  void startRanging();
  void stopRanging();

  // Returns immediately. False means "no new measurement yet" -- or that the
  // bus did not answer: check lost().
  bool dataReady();

  // Reads one finished measurement. Call only when dataReady() was true, and
  // follow it with clearInterrupt() or no further reading will arrive.
  uint16_t readDistanceMm();
  uint16_t readSignalRate();    // reflected signal: millions of counts/s x 128
  uint8_t  readRangeStatus();   // 0 = good; see the table in the .cpp

  void clearInterrupt();

  // True once any transfer has failed since begin(): the sensor is unplugged
  // or the bus is stuck.
  bool lost() const { return _lost; }

private:
  // A transfer here is at most six bytes, well under 1 ms at 400 kHz.
  static const uint TIMEOUT_US = 2000;

  i2c_inst_t* _bus  = nullptr;
  bool        _lost = false;

  // Every register on this chip has a 16-bit address, which is why we cannot
  // just use the ordinary "write a byte to a register" helpers.
  void    writeByte(uint16_t reg, uint8_t value);
  void    writeWord(uint16_t reg, uint16_t value);
  void    writeDword(uint16_t reg, uint32_t value);
  uint8_t  readByte(uint16_t reg);
  uint16_t readWord(uint16_t reg);
  bool    waitForData(uint32_t timeoutMs);
};
