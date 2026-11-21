#pragma once
#include "Config.h"

// Compile-time checks for pin conflicts and unsupported hardware settings.
// Included by Config.h. constexpr and static_assert do not add runtime code.

// True if no two entries of the array are equal. An ordinary loop -- what
// makes it usable in a static_assert is `constexpr`, which promises the
// compiler it can be run during the build.
constexpr bool allDifferent(const int* pins, int count) {
  for (int i = 0; i < count; i++)
    for (int j = i + 1; j < count; j++)
      if (pins[i] == pins[j]) return false;
  return true;
}

// The Pico W brings out GP0-GP22 and GP26-GP28. GP23, GP24 and GP25 exist but
// are wired to the WiFi chip and the LED on this board, so they are not yours
// to use, and GP29 is the internal voltage sense.
constexpr bool isUsablePin(int pin) {
  return (pin >= 0 && pin <= 22) || (pin >= 26 && pin <= 28);
}

constexpr bool allUsable(const int* pins, int count) {
  for (int i = 0; i < count; i++)
    if (!isUsablePin(pins[i])) return false;
  return true;
}

// Every pin this build actually drives. A device that is switched off in
// Config.h is left out, so turning the camera off frees its pins for
// something else without upsetting the check.
constexpr int kUsedPins[] = {
  LEFT_MOTOR_PIN_A,
  LEFT_MOTOR_PIN_B,
  RIGHT_MOTOR_PIN_A,
  RIGHT_MOTOR_PIN_B,
#if FRONT_SENSOR_ENABLED
  FRONT_SENSOR_PIN,
#endif
#if SIDE_SENSOR_ENABLED
  SIDE_SENSOR_SDA_PIN,
  SIDE_SENSOR_SCL_PIN,
#endif
#if DISPLAY_ENABLED
  DISPLAY_SDA_PIN,
  DISPLAY_SCL_PIN,
#endif
#if CAMERA_ENABLED
  CAMERA_RESET_PIN,
  CAMERA_XCLK_PIN,
  CAMERA_SDA_PIN,
  CAMERA_SCL_PIN,
  CAMERA_VSYNC_PIN,
  // All eight data pins, not just D0. They are consecutive and nothing says
  // so anywhere else, so a motor or the display sitting on D5 would otherwise
  // be invisible to every check in this file. Listing them is what makes
  // allDifferent() below able to see the whole ten-pin block the camera
  // occupies -- D0..D7, then PCLK and HREF at D0+8 and D0+9.
  CAMERA_D0_PIN,
  CAMERA_D0_PIN + 1,
  CAMERA_D0_PIN + 2,
  CAMERA_D0_PIN + 3,
  CAMERA_D0_PIN + 4,
  CAMERA_D0_PIN + 5,
  CAMERA_D0_PIN + 6,
  CAMERA_D0_PIN + 7,
  CAMERA_PCLK_PIN,
  CAMERA_HREF_PIN,
#endif
};

constexpr int kUsedPinCount = sizeof(kUsedPins) / sizeof(kUsedPins[0]);

static_assert(allDifferent(kUsedPins, kUsedPinCount),
              "Config.h: two things are wired to the same GP pin. "
              "Compare the pin map at the top of Config.h against the "
              "#defines below it.");

static_assert(allUsable(kUsedPins, kUsedPinCount),
              "Config.h: a pin number is not usable on a Pico W. "
              "Valid choices are GP0-GP22 and GP26-GP28; GP23-GP25 belong to "
              "the WiFi chip and the LED.");

// Two pins share a PWM slice when they form a GP2n / GP2n+1 pair. The motor
// pairs use that by design -- one slice, two channels, configured identically
// in Motors::begin(). What must not happen is the camera's XCLK landing on a
// slice a motor uses: the camera sets its own wrap and level, and whichever
// of the two begins last wins, so the other breaks silently.
constexpr bool sharesPwmSlice(int a, int b) { return a / 2 == b / 2; }

#if CAMERA_ENABLED
static_assert(!sharesPwmSlice(CAMERA_XCLK_PIN, LEFT_MOTOR_PIN_A) &&
              !sharesPwmSlice(CAMERA_XCLK_PIN, RIGHT_MOTOR_PIN_A),
              "Config.h: CAMERA_XCLK_PIN shares a PWM slice with a motor pin. "
              "The camera and the motors would overwrite each other's settings; "
              "move XCLK to a GP pair the motors do not use.");
#if MOTOR_DRIVER == MOTOR_DRIVER_DUAL_PWM
static_assert(!sharesPwmSlice(CAMERA_XCLK_PIN, LEFT_MOTOR_PIN_B) &&
              !sharesPwmSlice(CAMERA_XCLK_PIN, RIGHT_MOTOR_PIN_B),
              "Config.h: CAMERA_XCLK_PIN shares a PWM slice with a motor pin. "
              "The camera and the motors would overwrite each other's settings; "
              "move XCLK to a GP pair the motors do not use.");
#endif
#endif

#if CAMERA_ENABLED
// Not a free choice: drivers/HM01B0.cpp reads the pixel pins by offset from
// D0, so moving D0 means moving these two by the same amount.
static_assert(CAMERA_PCLK_PIN == CAMERA_D0_PIN + 8,
              "Config.h: CAMERA_PCLK_PIN must be CAMERA_D0_PIN + 8.");
static_assert(CAMERA_HREF_PIN == CAMERA_D0_PIN + 9,
              "Config.h: CAMERA_HREF_PIN must be CAMERA_D0_PIN + 9.");
// D0 and the seven data pins after it must all be real pins too. They are in
// kUsedPins above, so allUsable() already covers this; the message is worth
// having on its own, because "a pin number is not usable" does not tell you
// that it was the data block that ran off the end.
static_assert(isUsablePin(CAMERA_D0_PIN + 7),
              "Config.h: CAMERA_D0_PIN needs eight consecutive pins for D0-D7.");
#endif

// The display is a real I2C device on the RP2040's I2C1 controller, not a
// bit-banged bus like the camera's. Which pins I2C1 can use is fixed by the
// chip: SDA on GP2, 6, 10, 14, 18 or 26, SCL on GP3, 7, 11, 15, 19 or 27, and
// they have to be a neighbouring pair. Every one of those SDA pins is 2 more
// than a multiple of 4, and every SCL pin is 3 more, which is the whole of
// the test below. Get this wrong and i2c_init() succeeds, the pins do nothing,
// and the display simply never answers.
#if DISPLAY_ENABLED
static_assert(DISPLAY_SDA_PIN % 4 == 2 && DISPLAY_SCL_PIN == DISPLAY_SDA_PIN + 1,
              "Config.h: DISPLAY_SDA_PIN / DISPLAY_SCL_PIN are not a pin pair "
              "the RP2040's I2C1 controller can use. SDA must be GP2, 6, 10, "
              "14, 18 or 26, and SCL the pin straight after it. On the Grove "
              "shield, the I2C1 socket is GP6 and GP7.");
#endif

// The side sensor has the other controller, I2C0, and the same rule with the
// numbers shifted by two: SDA on GP0, 4, 8, 12, 16, 20 or 28 (a multiple of
// 4), SCL on the pin straight after. That the two controllers' SDA pins never
// coincide is what keeps the display and the sensor on separate buses.
#if SIDE_SENSOR_ENABLED
static_assert(SIDE_SENSOR_SDA_PIN % 4 == 0 && SIDE_SENSOR_SCL_PIN == SIDE_SENSOR_SDA_PIN + 1,
              "Config.h: SIDE_SENSOR_SDA_PIN / SIDE_SENSOR_SCL_PIN are not a pin "
              "pair the RP2040's I2C0 controller can use. SDA must be GP0, 4, 8, "
              "12, 16, 20 or 28, and SCL the pin straight after it. On this "
              "robot the sensor is on GP20 and GP21.");
#endif

// The safety rules only mean something if the ceiling is above the default.
static_assert(DEFAULT_DURATION_MS <= MAX_DURATION_MS,
              "Config.h: DEFAULT_DURATION_MS is above MAX_DURATION_MS, so "
              "every command would be silently cut short.");
static_assert(MAX_DURATION_MS > 0,
              "Config.h: MAX_DURATION_MS must be positive, or the robot can "
              "never move at all.");

static_assert(MOTOR_MAX_SPEED > 0 && MOTOR_MAX_SPEED <= 65535,
              "Config.h: MOTOR_MAX_SPEED is the PWM wrap value and must fit "
              "in 16 bits.");

// The camera serves pictures over HTTP, and HTTP needs the lwIP network stack
// -- which is only started by cyw43_arch_init(), inside WebRadio::begin().
// Under RADIO_SERIAL that never runs, so the camera would build fine and then
// hand a request to a stack that was never woken up. Caught here instead.
#if CAMERA_ENABLED && (RADIO == RADIO_SERIAL)
#error "Config.h: CAMERA_ENABLED needs RADIO_WIFI. The camera is served over \
HTTP, and RADIO_SERIAL never starts the network stack. Set RADIO to \
RADIO_WIFI, or CAMERA_ENABLED to false."
#endif

static_assert(RADIO == RADIO_SERIAL || RADIO == RADIO_WIFI,
              "Config.h: RADIO must be RADIO_SERIAL or RADIO_WIFI.");
