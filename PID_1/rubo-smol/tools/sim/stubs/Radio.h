#pragma once
// The Radio interface from robot/Radio.h, without the USB transport.
// tools/sim/main.cpp provides the radio.
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "SimWorld.h"

class Radio {
public:
  static constexpr size_t MAX_LINE = 256;
  virtual ~Radio() {}
  virtual bool begin() = 0;
  virtual void poll() = 0;
  virtual bool connected() = 0;
  virtual bool receive(char* line, size_t maxLength) = 0;
  virtual void send(const char* line) = 0;
  virtual const char* describe() = 0;
  virtual const char* describeShort() { return describe(); }
};
