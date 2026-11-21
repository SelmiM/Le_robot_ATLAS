#pragma once
// Simulated world for tools/sim.py: a robot on a flat floor beside a straight
// wall. The stub headers in tools/sim/stubs/ read and write this state in
// place of the Pico's hardware, so robot/Robot.h compiles unchanged.
//
// Coordinates are in mm and radians. The wall is the line y = 0, with gaps
// for doors. The robot starts below it (y < 0), heading along +x, so the wall
// is on its left. A positive turn rate turns left, towards the wall.
//
// Every value under "Model" is invented but plausible for the class chassis
// (4WD, TT gear motors, skid steering). They were chosen so that the
// behaviours described for TP5 appear; amplitudes and periods measured here
// will differ on the robot.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

typedef unsigned int uint;   // as in pico/types.h

namespace sim {

// ---- Model -------------------------------------------------------------
//
// Defaults for the SIM_ values. Any of them can be changed without editing:
//     python3 tools/sim.py -D SIM_MOTOR_DELAY_MS=50

// Wheels. Speed is zero up to the dead zone, then grows linearly with the
// command; it follows the command after a dead time, then with a lag.
#ifndef SIM_DEAD_ZONE
#define SIM_DEAD_ZONE        50     // |command| that just fails to turn a wheel
#endif
#ifndef SIM_MM_S_PER_STEP
#define SIM_MM_S_PER_STEP    2.5    // mm/s per command step: 150 gives 250 mm/s
#endif
#ifndef SIM_LEFT_STRENGTH
#define SIM_LEFT_STRENGTH    0.92   // left side 8% weaker: it drifts towards the wall
#endif
#ifndef SIM_MOTOR_DELAY_MS
#define SIM_MOTOR_DELAY_MS   100    // dead time: gear backlash, static friction
#endif
#ifndef SIM_MOTOR_LAG_MS
#define SIM_MOTOR_LAG_MS     80     // time constant of a wheel reaching its speed
#endif

// Skid steering scrubs the tyres sideways, so the robot turns as if its
// wheels were further apart than they are.
#ifndef SIM_TRACK_MM
#define SIM_TRACK_MM         360    // effective distance between left and right wheels
#endif

// Side sensor on the left flank, this far ahead of the point the robot turns
// about. The further forward, the earlier it sees the robot turning.
#ifndef SIM_SENSOR_AHEAD_MM
#define SIM_SENSOR_AHEAD_MM  60
#endif

constexpr double DEAD_ZONE       = SIM_DEAD_ZONE;
constexpr double MM_S_PER_STEP   = SIM_MM_S_PER_STEP;
constexpr double LEFT_STRENGTH   = SIM_LEFT_STRENGTH;
constexpr int    MOTOR_DELAY_MS  = SIM_MOTOR_DELAY_MS;
constexpr double MOTOR_LAG_MS    = SIM_MOTOR_LAG_MS;
constexpr double TRACK_MM        = SIM_TRACK_MM;
constexpr double SENSOR_AHEAD_MM = SIM_SENSOR_AHEAD_MM;

// Body, centred on the point the robot turns about.
constexpr double HALF_LENGTH_MM  = 100;
constexpr double HALF_WIDTH_MM   = 70;

// VL53L1X, short mode, as set up in robot/TofSensor.h.
constexpr uint32_t TOF_INTERVAL_MS   = 40;     // a measurement starts every 40 ms
constexpr uint32_t TOF_MEASURE_MS    = 33;     // and is ready 33 ms later
constexpr uint32_t TOF_POLL_MS       = 5;      // TofSensor polls this often
constexpr double   TOF_RANGE_MM      = 1300;   // further than this: no reading
constexpr double   TOF_MAX_ANGLE_DEG = 60;     // a wall more oblique than this returns too little light
constexpr double   TOF_HALF_CONE_DEG = 13.5;   // a 27 degree cone: it reports the nearest point in it
constexpr double   TOF_NOISE_MM      = 3;      // plus 0.5% of the distance
constexpr double   TOF_DROPOUT       = 0.01;   // share of readings lost for no visible reason

// Grove ultrasonic front sensor: nothing in front along an endless wall.
constexpr int FRONT_STATUS_NOTHING = -1;

// ---- State -------------------------------------------------------------

inline uint32_t now = 0;           // simulated milliseconds since boot

inline double x = 0, y = 0, heading = 0;
inline double leftSpeed = 0, rightSpeed = 0;   // mm/s, actual
inline int    leftCommand = 0, rightCommand = 0;

// Commands as the wheels will feel them, MOTOR_DELAY_MS late.
inline int pastLeft[MOTOR_DELAY_MS + 1]  = {};
inline int pastRight[MOTOR_DELAY_MS + 1] = {};

struct Door { double from, to; };
inline std::vector<Door> doors;

inline std::mt19937 rng;

inline uint32_t durationMs   = 20000;
inline FILE*    trace        = nullptr;
inline long     doneLines    = 0;      // telemetry lines with "done":true: each is a safety stop
inline long     sideReadings = 0;
inline long     sideMissing  = 0;
inline double   travelled    = 0;
inline uint32_t lastMovedAt  = 0;
inline long     callsThisTick = 0;

inline bool inDoor(double atX) {
  for (const Door& d : doors)
    if (atX >= d.from && atX <= d.to) return true;
  return false;
}

// Gap between the left flank and the wall, measured square to the wall.
inline double clearance() { return -y - HALF_WIDTH_MM; }

// What the side sensor sees from the current pose: distance in mm, or -1.
inline int sideRay() {
  double c = std::cos(heading), s = std::sin(heading);
  // Sensor position: ahead of the turning point, on the left flank.
  double sx = x + SENSOR_AHEAD_MM * c - HALF_WIDTH_MM * s;
  double sy = y + SENSOR_AHEAD_MM * s + HALF_WIDTH_MM * c;
  // The beam points left: direction (-sin, cos). The cone's nearest point
  // on the wall is less oblique than the beam itself.
  double towardsWall = c;
  if (towardsWall < std::cos(TOF_MAX_ANGLE_DEG * M_PI / 180)) return -1;
  if (sy >= 0) return -1;
  double oblique = std::fabs(heading) - TOF_HALF_CONE_DEG * M_PI / 180;
  double range = -sy / std::cos(oblique > 0 ? oblique : 0);
  if (range > TOF_RANGE_MM) return -1;
  if (inDoor(sx + sy / towardsWall * s)) return -1;   // where the beam's axis meets the wall

  std::normal_distribution<double> noise(0, TOF_NOISE_MM + 0.005 * range);
  std::uniform_real_distribution<double> unit(0, 1);
  if (unit(rng) < TOF_DROPOUT) return -1;
  int mm = (int)std::lround(range + noise(rng));
  return mm < 0 ? 0 : mm;
}

inline double wheelSpeed(int command, double strength) {
  double magnitude = std::abs(command) - DEAD_ZONE;
  if (magnitude <= 0) return 0;
  return (command > 0 ? 1 : -1) * magnitude * MM_S_PER_STEP * strength;
}

inline bool hitTheWall() {
  double c = std::cos(heading), s = std::sin(heading);
  for (int i = -1; i <= 1; i += 2)
    for (int j = -1; j <= 1; j += 2) {
      double cx = x + i * HALF_LENGTH_MM * c - j * HALF_WIDTH_MM * s;
      double cy = y + i * HALF_LENGTH_MM * s + j * HALF_WIDTH_MM * c;
      if (cy >= 0 && !inDoor(cx)) return true;
    }
  return false;
}

// Published by the TofSensor stub, recorded in the trace.
inline int  sideShownMm = -1;
inline bool sideShownOk = false;

inline void finish(const char* why) {
  if (trace) fclose(trace);
  printf("end      %s at %.2f s\n", why, now / 1000.0);
  printf("position x = %.0f mm, clearance %.0f mm, heading %.1f deg\n",
         x, clearance(), heading * 180 / M_PI);
  printf("driven   %.2f m\n", travelled / 1000);
  printf("side     %ld readings, %ld missing\n", sideReadings, sideMissing);
  printf("safety   %ld stops by enforceSafety() (telemetry lines with \"done\":true)\n",
         doneLines);
  std::exit(0);
}

// One millisecond of simulated time.
inline void tick() {
  callsThisTick = 0;
  constexpr double dt = 0.001;
  double k = 1.0 / MOTOR_LAG_MS;   // per ms
  int slot = now % (MOTOR_DELAY_MS + 1);
  pastLeft[slot]  = leftCommand;
  pastRight[slot] = rightCommand;
  int felt = (now + 1) % (MOTOR_DELAY_MS + 1);    // the oldest one kept
  leftSpeed  += (wheelSpeed(pastLeft[felt],  LEFT_STRENGTH) - leftSpeed)  * k;
  rightSpeed += (wheelSpeed(pastRight[felt], 1.0)           - rightSpeed) * k;

  double v = (leftSpeed + rightSpeed) / 2;
  heading += (rightSpeed - leftSpeed) / TRACK_MM * dt;
  x += v * std::cos(heading) * dt;
  y += v * std::sin(heading) * dt;
  travelled += std::abs(v) * dt;
  if (leftCommand != 0 || rightCommand != 0) lastMovedAt = now;

  now += 1;

  if (trace && now % 10 == 0)
    fprintf(trace, "%u,%.1f,%.1f,%.2f,%d,%d,%d,%d,%.0f,%.0f\n",
            now, x, clearance(), heading * 180 / M_PI,
            leftCommand, rightCommand, sideShownMm, sideShownOk ? 1 : 0,
            leftSpeed, rightSpeed);

  if (hitTheWall())             finish("HIT THE WALL");
  if (clearance() > 3000)       finish("more than 3 m from the wall");
  if (now >= durationMs)        finish("time up");
  if (now > 3000 && now - lastMovedAt > 3000)
    finish("the wheels have not turned for 3 s");
}

}  // namespace sim

// ---- What the Pico SDK would provide -----------------------------------

// Time passes while the robot waits, but the loop does not run: nothing
// checks the deadlines and nothing reads the sensors.
inline void sleep_ms(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) sim::tick(); }
inline void sleep_us(uint64_t us) { sleep_ms((uint32_t)(us / 1000)); }
inline void tight_loop_contents() {}
