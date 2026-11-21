#pragma once
// Simulated clock: see tools/sim/SimWorld.h.
#include "SimWorld.h"

inline uint32_t nowMs() {
  // A loop that waits by watching the clock would never let simulated time
  // pass. Let it pass, as it would on the robot.
  if (++sim::callsThisTick > 100000) sim::tick();
  return sim::now;
}
