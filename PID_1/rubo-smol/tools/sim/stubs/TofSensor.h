#pragma once
// Simulated VL53L1X: same interface as robot/TofSensor.h, same timing. Each
// measurement sees the wall from where the robot was when it started.
#include "SimWorld.h"
#include "Config.h"

class TofSensor {
public:
  static const uint32_t INTERVAL_MS      = sim::TOF_INTERVAL_MS;
  static const uint32_t POLL_INTERVAL_MS = sim::TOF_POLL_MS;

  // As on the robot, a sensor switched off in Config.h never reads anything.
  bool begin() {
    _connected = SIDE_SENSOR_ENABLED;
    _nextStart = sim::now;
    return _connected;
  }

  void update() {
    if (!_connected) return;
    if (sim::now - _lastPollMs < POLL_INTERVAL_MS) return;
    _lastPollMs = sim::now;

    if (_pending && (int32_t)(sim::now - _readyAt) >= 0) {
      _pending    = false;
      _distanceMm = _measured;
      _status     = _measured >= 0 ? 0 : 2;     // 2: not enough light came back
      _quality    = _measured >= 0 ? 4000 : 0;
      sim::sideReadings++;
      if (_measured < 0) sim::sideMissing++;
      sim::sideShownMm = _distanceMm;
      sim::sideShownOk = valid();
    }

    if ((int32_t)(sim::now - _nextStart) >= 0) {
      _measured  = sim::sideRay();
      _readyAt   = sim::now + sim::TOF_MEASURE_MS;
      _pending   = true;
      _nextStart += INTERVAL_MS;
    }
  }

  bool connected() const { return _connected; }
  bool valid() const { return _connected && _status == 0; }

  int distanceMm() const { return _distanceMm; }
  int quality()    const { return _quality; }
  int status()     const { return _status; }

private:
  uint32_t _lastPollMs = 0;
  uint32_t _nextStart  = 0;
  uint32_t _readyAt    = 0;
  bool     _pending    = false;
  int      _measured   = -1;

  bool _connected  = false;
  int  _distanceMm = -1;
  int  _quality    = -1;
  int  _status     = -1;
};
