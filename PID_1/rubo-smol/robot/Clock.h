#pragma once
#include "pico/stdlib.h"

// Milliseconds since boot; the 32-bit counter wraps after about 49 days.
//
//      uint32_t now = nowMs();
//
// Compare elapsed time by subtracting timestamps, not by comparing against
// a future timestamp:
//
//      if (nowMs() - startedAt > 1000)          // right: still works at the wrap
//      if (nowMs() > startedAt + 1000)          // wrong: breaks at the wrap
//
// Unsigned subtraction handles wraparound for intervals below one full cycle.

inline uint32_t nowMs() {
  return to_ms_since_boot(get_absolute_time());
}
