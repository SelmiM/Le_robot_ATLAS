#pragma once
// Simulated front sensor: nothing ahead along an endless wall, so it never
// reads anything.
#include "SimWorld.h"
#include "Config.h"

class UltrasonicSensor {
public:
  bool begin()            { _connected = FRONT_SENSOR_ENABLED; return _connected; }
  void update()           {}
  bool connected() const  { return _connected; }
  bool valid() const      { return false; }
  int  distanceMm() const { return -1; }
  int  quality() const    { return -1; }
  int  status() const     { return sim::FRONT_STATUS_NOTHING; }

private:
  bool _connected = false;
};
