// Entry point: initialize devices and the selected radio, then poll both
// without blocking. See README.md for setup and robot/Config.h for hardware
// settings. Missions and movement deadlines live in Robot.h.

#include <cstdio>
#include "pico/stdlib.h"

#include "Config.h"
#include "Radio.h"
#include "Robot.h"
#if RADIO == RADIO_WIFI
#include "WebRadio.h"
#endif

// Keep the radio and robot alive for the duration of the program. Robot uses
// the Radio interface, regardless of whether USB serial or WiFi is selected.
#if RADIO == RADIO_SERIAL
SerialRadio    radio;
#else
WebRadio       radio;
#endif

Robot          robot(radio);


// Report which configured devices responded at startup.
static void report(const char* name, bool enabled, bool working) {
  printf("  %s : %s\n", name,
         !enabled ? "off (see Config.h)"
                  : working ? "ok"
                            : "ENABLED BUT NOT RESPONDING - check the wiring");
}


int main() {
  stdio_init_all();          // printf goes out over the USB cable

  // Allow time for the host to open USB serial before printing the banner.
  sleep_ms(2000);

  robot.begin();             // initialize motors before sensors

#if RADIO == RADIO_WIFI
  // WebRadio serves snapshots from the camera owned by Robot.
  radio.serveCamera(robot.camera);
#endif

  bool online = radio.begin();
  if (online) {
    printf("Robot ready.\n");
    printf("  radio    : %s\n", radio.describe());

    // Keep the WiFi address on the display until the first command arrives.
    robot.display.line(0, "Robot ready");
    robot.display.line(1, radio.describeShort());
  } else {
    // Without a link, there is no way to receive a stop command.
    printf("Could not bring up the radio. Check RADIO in Config.h"
           " (and WIFI_SSID / WIFI_PASSWORD if it is set to RADIO_WIFI).\n");
  }

  // Surface wiring faults before accepting commands.
  report("front distance sensor", FRONT_SENSOR_ENABLED, robot.front.connected());
  report("side distance sensor",  SIDE_SENSOR_ENABLED,  robot.side.connected());
  report("display",               DISPLAY_ENABLED,      robot.display.connected());
  report("camera",                CAMERA_ENABLED,       robot.camera.connected());

  // Keep the loop non-blocking: Robot::update() checks movement deadlines.
  while (true) {
    // A failed WiFi initialization leaves no network stack to poll.
    if (online) radio.poll();

    robot.update();
  }
}
