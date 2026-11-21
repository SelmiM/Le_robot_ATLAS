#pragma once
// Simulated camera: there is none.
#include <cstddef>
#include <cstdint>
#include "SimWorld.h"

class Camera {
public:
  bool begin()                    { return false; }
  bool connected() const          { return false; }
  bool capture()                  { return false; }
  bool hasPicture() const         { return false; }
  int  width() const              { return 0; }
  int  height() const             { return 0; }
  int  pixel(int, int) const      { return 0; }
  int  brightness() const         { return 0; }
  const uint8_t* data() const     { return nullptr; }
  size_t size() const             { return 0; }
};
