#pragma once
// Simulated display: there is none.
#include "SimWorld.h"

class Display {
public:
  static const int COLUMNS = 16;
  static const int ROWS    = 2;

  bool begin()                                   { return false; }
  bool connected() const                         { return false; }
  void line(int, const char*)                    {}
  bool alreadyShowing(int, const char*) const    { return true; }
  void clear()                                   {}
};
