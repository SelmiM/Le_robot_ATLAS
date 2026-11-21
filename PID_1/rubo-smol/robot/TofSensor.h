#pragma once
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "Config.h"
#include "Clock.h"
#include "drivers/VL53L1X.h"

/**
 * VL53L1X time-of-flight distance sensor on I2C0, address 0x29.
 *
 *     if (side.valid() && side.distanceMm() < 200) { ... }
 *
 * The chip measures in the background; update() polls for a completed reading
 * using timed I2C transfers. begin() and reconnection can block briefly while
 * it initializes. Short mode reaches about 1.3 m and tolerates daylight better
 * than long mode. Dark surfaces and sunlight can weaken its signal; check
 * valid() and quality() before steering from a reading.
 */
class TofSensor {
public:
  // 400 kHz: the chip allows it, and it keeps every poll short.
  static const uint BUS_HZ = 400 * 1000;

  // One measurement takes 33 ms; the next starts every INTERVAL_MS. The
  // chip insists on a gap at least as long as the measurement.
  static const uint32_t INTERVAL_MS = 40;

  // Several polls per measurement, so none waits long to be collected;
  // few enough that the bus is idle almost all the time.
  static const uint32_t POLL_INTERVAL_MS = 5;

  // Returns false if the sensor is switched off in Config.h, or nothing
  // answered. Blocks for up to ~120 ms (see above).
  bool begin() {
    if (!SIDE_SENSOR_ENABLED) return false;

    i2c_init(i2c0, BUS_HZ);
    gpio_set_function(SIDE_SENSOR_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SIDE_SENSOR_SCL_PIN, GPIO_FUNC_I2C);
    // I2C lines are pulled up, never driven high: any device may hold one low
    // at any time, which is how a bus with no chip select works out whose
    // turn it is. The breakout has its own pull-ups, so these are belt and
    // braces.
    gpio_pull_up(SIDE_SENSOR_SDA_PIN);
    gpio_pull_up(SIDE_SENSOR_SCL_PIN);

    return start();
  }

  // Returns immediately (see the exception above).
  void update() {
    if (!_connected) {
      lookAgain();
      return;
    }

    if (nowMs() - _lastPollMs < POLL_INTERVAL_MS) return;
    _lastPollMs = nowMs();

    if (_chip.dataReady()) {
      int mm     = _chip.readDistanceMm();
      int signal = _chip.readSignalRate();
      int status = _chip.readRangeStatus();

      // Tells the chip we have this reading, so it can hand over the next.
      // Forget it and dataReady() never fires again.
      _chip.clearInterrupt();

      if (!_chip.lost()) {
        // ST's own unit: thousands of counts per second, which is the raw
        // register (millions, with 7 fractional bits) times 1000 / 128 --
        // eight, near enough, and eight is what ST's driver uses.
        _quality    = signal * 8;
        _status     = status;
        _distanceMm = (status == 0) ? mm : -1;
      }
    }

    // Any transfer that failed means the sensor has gone: unplugged, or a
    // loose wire. Say so, and start looking for it again.
    if (_chip.lost()) {
      _connected  = false;
      _status     = -1;
      _distanceMm = -1;
      _quality    = -1;
    }
  }

  // Has the sensor answered yet? False at startup can turn true later.
  bool connected() const { return _connected; }

  // Can the LAST reading be trusted? Status 0 is the chip's own "good". The
  // others are listed in drivers/VL53L1X.cpp; the one you will see most is 2,
  // "not enough light came back", which is what nothing in range looks like.
  bool valid() const { return _connected && _status == 0; }

  int distanceMm() const { return _distanceMm; }   // -1 unless valid()
  int quality()    const { return _quality; }      // signal, thousands of counts/s
  int status()     const { return _status; }       // 0 good, anything else is a problem

private:
  // Wakes the chip and sets it measuring. begin() and lookAgain() both come
  // here.
  bool start() {
    if (!_chip.begin(i2c0)) {
      _connected = false;
      return false;
    }
    _chip.setMode(VL53L1X::Mode::Short);
    _chip.setIntervalMs(INTERVAL_MS);
    _chip.startRanging();

    _connected = !_chip.lost();
    return _connected;
  }

  // A sensor that missed its chance in begin() gets another one every
  // second, as in UltrasonicSensor. Asking costs one short transfer that
  // nobody acknowledges; only a sensor that answers pays for start().
  //
  // Only for an enabled sensor: a disabled one's pins may be wired to
  // something else entirely.
  void lookAgain() {
    if (!SIDE_SENSOR_ENABLED) return;

    if (nowMs() - _lastLookMs < RETRY_INTERVAL_MS) return;
    _lastLookMs = nowMs();

    uint8_t probe;
    if (i2c_read_timeout_us(i2c0, VL53L1X::ADDRESS, &probe, 1, false, PROBE_TIMEOUT_US) != 1)
      return;
    start();
  }

  static const uint32_t RETRY_INTERVAL_MS = 1000;
  static const uint     PROBE_TIMEOUT_US  = 1000;

  VL53L1X _chip;

  uint32_t _lastPollMs = 0;
  uint32_t _lastLookMs = 0;

  bool _connected  = false;
  int  _distanceMm = -1;
  int  _quality    = -1;
  int  _status     = -1;
};
