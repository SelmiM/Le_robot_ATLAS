// Runs robot/Robot.h against the simulated world in stubs/SimWorld.h.
// Built and started by tools/sim.py, which explains the options.
//
//     sim trace.csv duration_ms start_mm heading_deg mission seed [door_from:door_to ...]

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Robot.h"     // robot/Robot.h, copied next to this file by tools/sim.py

// Sends {"mission": N} once, then stays silent and connected.
class SimRadio : public Radio {
public:
  explicit SimRadio(int mission) { snprintf(_command, sizeof(_command), "{\"mission\":%d}", mission); }

  bool begin() override { return true; }
  void poll() override {}
  bool connected() override { return true; }

  bool receive(char* line, size_t maxLength) override {
    if (_sent) return false;
    _sent = true;
    snprintf(line, maxLength, "%s", _command);
    return true;
  }

  void send(const char* line) override {
    if (strstr(line, "\"done\":true")) sim::doneLines++;
    if (strstr(line, "\"error\""))     printf("robot    %s\n", line);
  }

  const char* describe() override { return "simulation"; }

private:
  char _command[32];
  bool _sent = false;
};

int main(int argc, char** argv) {
  if (argc < 7) {
    fprintf(stderr, "run this through tools/sim.py\n");
    return 2;
  }

  sim::trace      = fopen(argv[1], "w");
  sim::durationMs = (uint32_t)atol(argv[2]);
  double startMm  = atof(argv[3]);
  sim::heading    = atof(argv[4]) * M_PI / 180;
  int mission     = atoi(argv[5]);
  sim::rng.seed((unsigned)atol(argv[6]));
  for (int i = 7; i < argc; i++) {
    double from, to;
    if (sscanf(argv[i], "%lf:%lf", &from, &to) == 2) sim::doors.push_back({from, to});
  }

  if (!sim::trace) {
    perror(argv[1]);
    return 2;
  }
  fprintf(sim::trace, "t_ms,x_mm,clearance_mm,heading_deg,left_cmd,right_cmd,"
                      "side_mm,side_ok,left_mm_s,right_mm_s\n");

  // The robot starts with its left flank startMm from the wall.
  sim::y = -(startMm + sim::HALF_WIDTH_MM);

  if (!SIDE_SENSOR_ENABLED)
    printf("warning  SIDE_SENSOR_ENABLED is false in Config.h: the side sensor"
           " reads nothing, as on the robot\n");

  SimRadio radio(mission);
  Robot robot(radio);
  robot.begin();

  for (;;) {
    robot.update();
    sim::tick();       // calls sim::finish(), which exits, when the run is over
  }
}
