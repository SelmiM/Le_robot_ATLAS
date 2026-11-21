#pragma once
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "Config.h"

/**
 * Grove Ultrasonic Ranger V2.0 on one digital pin.
 *
 *     if (front.valid() && front.distanceMm() < 200) { ... }
 *
 * The module returns echo time as a pulse width (about 5.8 us per mm).
 * Interrupts record the echo edges so the main loop does not wait for them.
 * Use a digital Grove socket: the module would pull SCL low in an I2C socket.
 * Angled surfaces can reflect the chirp away and cause invalid readings.
 */
class UltrasonicSensor {
public:
  // The datasheet asks for at least 60 ms between chirps: any sooner and the
  // tail of the last one comes back as the head of the next.
  static const uint32_t MEASUREMENT_INTERVAL_MS = 60;

  // 350 cm is the module's limit; past that the number is an artefact.
  static const uint32_t MAX_RANGE_MM    = 3500;
  static const uint32_t ECHO_TIMEOUT_US = 30000;   // beyond any real echo

  // Nothing to configure and no ID register to read, so "did it answer?" can
  // only mean "did a chirp come back?". We ask a few times over ~300 ms; in
  // any normal room something is within 3.5 m. A sensor that is unplugged
  // never drives the line, so it never answers, and startup says so.
  //
  // Startup is not its only chance: see lookAgain().
  //
  // Returns false if the sensor is switched off in Config.h, or did not
  // answer.
  bool begin() {
    if (!FRONT_SENSOR_ENABLED) return false;

    gpio_init(PIN);
    gpio_set_dir(PIN, GPIO_IN);

    // A floating input is not a reading. The module drives the line both
    // ways, so this only matters while nothing is plugged in -- and then it
    // holds the line low, which reads as "no echo" instead of as noise.
    gpio_pull_down(PIN);

    _self = this;
    gpio_set_irq_enabled_with_callback(PIN,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &UltrasonicSensor::onEdge);

    for (int attempt = 0; attempt < 5; attempt++) {
      trigger();
      // begin() is allowed to wait -- it runs once, before the wheels can
      // turn. update() is the one that must never block.
      absolute_time_t giveUp = make_timeout_time_us(ECHO_TIMEOUT_US);
      while (!_echoComplete && absolute_time_diff_us(get_absolute_time(), giveUp) > 0)
        tight_loop_contents();

      if (takeReading()) {
        _connected = true;
        return true;
      }
      sleep_ms(MEASUREMENT_INTERVAL_MS);
    }

    _connected = false;
    return false;
  }

  // Returns immediately, always. Sends a chirp if one is due, and picks up
  // the previous answer if the interrupt has finished with it.
  void update() {
    if (!_connected) {
      lookAgain();
      return;
    }

    if (_waiting) {
      if (takeReading()) return;

      // No echo inside the timeout means nothing is in range -- a normal
      // answer for this sensor, not a fault. Say so and let the next chirp
      // go out on schedule.
      if (time_us_32() - _triggeredAtUs > ECHO_TIMEOUT_US) {
        _waiting    = false;
        _status     = 1;
        _distanceMm = -1;
      }
      return;
    }

    if (time_us_32() - _triggeredAtUs >= MEASUREMENT_INTERVAL_MS * 1000)
      trigger();
  }

  // Has the sensor answered yet? False at startup can turn true later.
  bool connected() const { return _connected; }

  // Can the LAST reading be trusted? Status 0 means a chirp came back inside
  // the module's range; anything else means nothing was there to reflect it.
  bool valid() const { return _connected && _status == 0; }

  int distanceMm() const { return _distanceMm; }
  int quality()    const { return _quality; }   // always -1: see below
  int status()     const { return _status; }

private:
  // 5.8 us per mm, written so it stays in integers: us * 10 / 58.
  static int toMillimetres(uint32_t echoUs) { return (int)((echoUs * 10) / 58); }

  void trigger() {
    // Deaf while we speak. The signal pin is shared, so the 10 us pulse below
    // raises and drops the very line the interrupt is watching -- and an
    // interrupt cannot tell our voice from the module's. Left armed, it
    // times our own trigger and reports a confident 2 mm forever.
    _armed        = false;
    _echoComplete = false;
    _waiting      = true;
    _echoStartUs  = 0;
    _echoEndUs    = 0;

    // Drive the line: a clean low, then the 10 us pulse the module is
    // listening for, then low again.
    gpio_set_dir(PIN, GPIO_OUT);
    gpio_put(PIN, 0);
    busy_wait_us(2);
    gpio_put(PIN, 1);
    busy_wait_us(10);
    gpio_put(PIN, 0);

    // Let go, so the module can answer on the same wire, and only now start
    // listening. The module waits ~450 us before its burst comes back, which
    // is ample time for the line to settle.
    gpio_set_dir(PIN, GPIO_IN);
    _triggeredAtUs = time_us_32();
    _armed         = true;
  }

  // A sensor that missed its chance in begin() -- plugged in after power-up,
  // a loose contact, a check that simply failed -- would otherwise be off
  // until the next reboot, and with the cable gone nobody reads the startup
  // message that says so. So it gets one chirp a second, sent and forgotten
  // like any other, until one comes back.
  //
  // Only for an enabled sensor: a disabled one's pin may be wired to
  // something else entirely, and must not be pulsed.
  void lookAgain() {
    if (!FRONT_SENSOR_ENABLED) return;

    if (_waiting) {
      if (takeReading()) {        // any echo at all: the module is there
        _connected = true;
        return;
      }
      if (time_us_32() - _triggeredAtUs > ECHO_TIMEOUT_US) _waiting = false;
      return;
    }

    if (time_us_32() - _triggeredAtUs >= RETRY_INTERVAL_MS * 1000) trigger();
  }

  static const uint32_t RETRY_INTERVAL_MS = 1000;

  // True if a finished echo was waiting for us.
  bool takeReading() {
    if (!_echoComplete) return false;

    _echoComplete = false;
    _waiting      = false;

    uint32_t echoUs = _echoEndUs - _echoStartUs;
    int mm = toMillimetres(echoUs);

    // Past the module's range the number is an artefact, not a measurement.
    if (mm <= 0 || mm > (int)MAX_RANGE_MM) {
      _status     = 2;
      _distanceMm = -1;
      return true;
    }

    _distanceMm = mm;
    _status     = 0;
    return true;
  }

  void edge(uint32_t events) {
    if (!_armed) return;

    if (events & GPIO_IRQ_EDGE_RISE) {
      _echoStartUs = time_us_32();
    } else if (events & GPIO_IRQ_EDGE_FALL) {
      // Only interested in the fall that closes an echo we started.
      if (_echoStartUs != 0) {
        _echoEndUs    = time_us_32();
        _echoComplete = true;
        _armed        = false;   // one echo per chirp
      }
    }
  }

  // The SDK's callback is a plain function, one for the whole chip, so it
  // has no object to call edge() on. begin() leaves one here for it.
  inline static UltrasonicSensor* _self = nullptr;

  static void onEdge(uint gpio, uint32_t events) {
    if (_self != nullptr && gpio == PIN) _self->edge(events);
  }

  static const uint PIN = FRONT_SENSOR_PIN;

  volatile uint32_t _echoStartUs  = 0;
  volatile uint32_t _echoEndUs    = 0;
  volatile bool     _echoComplete = false;
  volatile bool     _armed        = false;

  uint32_t _triggeredAtUs = 0;
  bool     _waiting       = false;

  bool _connected  = false;
  int  _distanceMm = -1;
  int  _quality    = -1;   // no equivalent: it hears an echo or it does not
  int  _status     = -1;
};

// Try this:
//
//   1. A single bad reading should not panic the robot. Keep the last three
//      readings and report the middle one (a median filter). Three lines,
//      and it removes almost all the noise.
//   2. Add a lastUpdateMs() so callers can tell a fresh reading from a stale
//      one -- valid() only says the last reading was good, not that it was
//      recent.
//   3. MEASUREMENT_INTERVAL_MS is 60 ms because a chirp needs time to die
//      away. How far down can you take it before one chirp starts hearing
//      the last one? Measure it, do not guess.
