#pragma once

// A robot/Config.local.h, if present, replaces this whole file. Copy this
// file there to keep a robot's pins and WiFi credentials out of git; the
// local copy is listed in .gitignore. Delete it to return to these defaults.
#if __has_include("Config.local.h")
#include "Config.local.h"
#else

// Hardware configuration for the Raspberry Pi Pico W/WH. Change pin numbers,
// device switches and timing here; rebuild and reflash to apply changes.
// ConfigCheck.h validates the supported pin combinations at compile time.
//
// Pin map (GP numbers, not physical pin numbers):
//
//    GP2  left motor  A  (L298N IN1)   GP6  display SDA   (I2C1 socket)
//    GP3  left motor  B  (L298N IN2)   GP7  display SCL   (I2C1 socket)
//    GP26 right motor A  (L298N IN3)
//    GP27 right motor B  (L298N IN4)   GP18 front distance sensor SIG
//                                           (ultrasonic, D18 socket)
//                                      GP20 side distance sensor SDA
//                                      GP21 side distance sensor SCL
//                                           (VL53L1X, I2C0, Pico header
//                                            pins 26 and 27)
//
//    With CAMERA_ENABLED:
//
//    GP0  camera RESET   GP4  camera SDA   GP8-GP15 camera D0-D7
//    GP1  camera XCLK    GP5  camera SCL   GP16 camera PCLK
//                                          GP17 camera HREF
//                                          GP22 camera VSYNC
//
//    Free: GP19, GP28. Leave the D20 socket empty while GP20/GP21 are used
//    by the side sensor.


// Radio
//
//  RADIO_SERIAL: commands and telemetry over USB serial at 115200 baud.
//  RADIO_WIFI:   the robot serves http://<robot-ip>/ on the local network.
//  The same commands work over both links; see Radio.h.

#define RADIO_SERIAL  1
#define RADIO_WIFI    2

#define RADIO  RADIO_WIFI

// The camera serves its pictures over HTTP, which needs the network stack
// that only RADIO_WIFI starts up. CAMERA_ENABLED and RADIO_SERIAL together
// are refused by ConfigCheck.h rather than misbehaving at run time.


// WiFi (RADIO_WIFI only)
//
//  Join an existing 2.4 GHz network accessible to the phone. For an open
//  network use nullptr, not "", as WIFI_PASSWORD.

#define WIFI_SSID      "RUBOLINK"
#define WIFI_PASSWORD  nullptr

#define HTTP_PORT      80     // the phone page: http://<robot-ip>/

// Shown on the phone page. Use only letters, digits and dashes: the name is
// inserted into a JSON string without escaping.
#define ROBOT_NAME     "ATLAS"


// Motors
//
//  DUAL_PWM: pin A drives forward, pin B backward. Use this for the JoyIT
//  L298N breakout (IN1–IN4); leave its ENA and ENB jumpers fitted.
//  DIR_PWM: pin A controls speed, pin B direction. Requires driver-board
//  direction logic, as on the older DRI0009 shield.
//
//      left  motors:  IN1 = GP2    IN2 = GP3
//      right motors:  IN3 = GP26   IN4 = GP27
//
//  The two motors on each side share one output pair. If they turn against
//  each other, swap one motor's wires; REVERSED changes the whole side.
//  Connect L298N GND to Pico GND even though the batteries are separate.
//  Pico outputs are 3.3 V, above the L298's 2.3 V input minimum.

#define MOTOR_DRIVER_DIR_PWM   1
#define MOTOR_DRIVER_DUAL_PWM  2

#define MOTOR_DRIVER  MOTOR_DRIVER_DUAL_PWM

#define LEFT_MOTOR_PIN_A   2      // L298N IN1
#define LEFT_MOTOR_PIN_B   3     // L298N IN2
#define RIGHT_MOTOR_PIN_A  26     // L298N IN3
#define RIGHT_MOTOR_PIN_B  27     // L298N IN4


// Reverse a complete side if its motors turn backward.
#define LEFT_MOTOR_REVERSED   true
#define RIGHT_MOTOR_REVERSED  true

// Speeds are -255 .. +255; the minimum speed that turns a motor varies.
#define MOTOR_MAX_SPEED     255

// 20 kHz keeps motor PWM above most people's hearing range.
#define MOTOR_PWM_FREQ_HZ   20000


// Distance sensors
//
//  FRONT: Grove Ultrasonic Ranger V2.0, one signal pin, 2–350 cm range.
//  SIDE: VL53L1X breakout marked "VL53L0/1XV2", on I2C0 at address 0x29.
//  Mount the side sensor on the left flank, pointing outwards for wall following.
//  The front sensor uses a digital Grove socket:
//
//      D16 socket   GP16        D18 socket   GP18        D20 socket   GP20
//
//  Never plug the ultrasonic module into I2C: its connector pulls SCL low.
//
//  The side sensor uses four jumper wires into the Pico header:
//
//      VIN  ->  pin 36, 3V3_OUT          SCL  ->  pin 27, GP21
//      GND  ->  pin 28, GND              SDA  ->  pin 26, GP20
//
//  Leave GPIO1 and XSHUT disconnected and the D20 socket empty: a Grove I2C
//  cable on D20 would reverse SDA and SCL. Disable any sensor not installed.
#define FRONT_SENSOR_ENABLED  true
#define FRONT_SENSOR_PIN      18      // D18 socket

#define SIDE_SENSOR_ENABLED   false
#define SIDE_SENSOR_SDA_PIN   20      // I2C0, Pico header pin 26
#define SIDE_SENSOR_SCL_PIN   21      // I2C0, Pico header pin 27


// Display (Grove 16x2 LCD, single colour)
//
//  Connect to the I2C1 socket (GP6/GP7). Display.h supports the AIP31068L
//  text controller at 0x3E. On a Grove RGB LCD, text works but backlight
//  colour is not controlled; PCF8574 backpacks use a different protocol.

#define DISPLAY_ENABLED   true

#define DISPLAY_SDA_PIN   6       // I2C1 socket
#define DISPLAY_SCL_PIN   7       // I2C1 socket
#define DISPLAY_ADDRESS   0x3E    // AIP31068L, fixed in the chip

// Display writes block the loop briefly; limit refreshes to this interval.
#define DISPLAY_REFRESH_MS  500


// Camera (optional ArduCAM HM01B0)
//
//  Enabling the camera reserves about 79 KB of RAM for a frame. Ten pins
//  must be consecutive: eight data pins, then PCLK and HREF:
//
//      PCLK must be D0 + 8      HREF must be D0 + 9
//
//  GP8–GP17 occupy the shield's I2C0 and D16 sockets; camera control pins
//  also occupy UART0/1. Its SDA/SCL use GPIO, not the Pico's I2C controllers.
//  The side sensor on GP20/GP21 and display on GP6/GP7 remain available.

#define CAMERA_ENABLED    false   // flip to true once the camera is wired up

#define CAMERA_RESET_PIN  0       // UART0 socket
#define CAMERA_XCLK_PIN   1       // UART0 socket
#define CAMERA_SDA_PIN    4       // UART1 socket
#define CAMERA_SCL_PIN    5       // UART1 socket
#define CAMERA_D0_PIN     8       // D0-D7 run GP8..GP15 (I2C0 socket, and on)
#define CAMERA_PCLK_PIN   16      // = CAMERA_D0_PIN + 8, for reference only
#define CAMERA_HREF_PIN   17      // = CAMERA_D0_PIN + 9, for reference only
#define CAMERA_VSYNC_PIN  22


// Safety and timing
//
//  Each movement has a deadline. Repeated commands or an active mission can
//  extend movement while the link is up; a lost link stops the wheels.

// How long a movement command runs when it does not say. Sending
//     {"left": 200, "right": 200}
// drives forward for this long, then stops on its own.
#define DEFAULT_DURATION_MS    1000

// A command asking for longer is capped at this duration.
#define MAX_DURATION_MS        5000

// How often the robot reports what its sensors see, in ms. 0 disables it.
// You can also change this while running, with {"telemetry": 500}.
#define DEFAULT_TELEMETRY_MS   200

// Without a page request for this long, WebRadio reports a lost link.
#define WEB_TIMEOUT_MS         1500


// Validate pin choices and incompatible options at compile time.
#include "ConfigCheck.h"

#endif  // Config.local.h


//{"left": 200, "right": 200 ,"duration":15000}
//{"left": -200, "right": 200}
//{"left": 0, "right": 200}
//{"left": 200, "right": 0}
//{"left": 200, "right": -200}