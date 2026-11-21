#pragma once
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "Config.h"

// A misspelled or missing MOTOR_DRIVER would otherwise evaluate to 0 and take
// the #else branch below in silence -- driving a one-wire direction pattern
// into a board that has no inverter to make sense of it, and leaving one input
// of each pair floating. Fail the build instead.
#if MOTOR_DRIVER != MOTOR_DRIVER_DIR_PWM && MOTOR_DRIVER != MOTOR_DRIVER_DUAL_PWM
#error "MOTOR_DRIVER in Config.h must be MOTOR_DRIVER_DIR_PWM or MOTOR_DRIVER_DUAL_PWM"
#endif

/**
 * Motors controls the left and right sides with signed speeds (-255..255).
 * Positive drives forward, negative backward, and zero stops the side.
 *
 *     motors.set(200, 200);    // straight ahead
 *     motors.set(200, -200);   // spin on the spot, clockwise
 *     motors.set(200, 100);    // curve to the right
 *     motors.stop();
 *
 * The RP2040's PWM hardware switches motor inputs at MOTOR_PWM_FREQ_HZ.
 * wrap sets the counter range, clkdiv the frequency, and level the duty cycle.
 * Pin-to-slice assignments come from pwm_gpio_to_slice_num().
 */
class Motors {
public:
  // Called once at startup, from Robot::begin().
  void begin() {
    setupPwmPin(LEFT_MOTOR_PIN_A);
    setupPwmPin(RIGHT_MOTOR_PIN_A);

#if MOTOR_DRIVER == MOTOR_DRIVER_DUAL_PWM
    // Both pins carry a speed, so both need PWM.
    setupPwmPin(LEFT_MOTOR_PIN_B);
    setupPwmPin(RIGHT_MOTOR_PIN_B);
#else
    // The B pins only say which way to turn: plain on/off outputs.
    setupDigitalPin(LEFT_MOTOR_PIN_B);
    setupDigitalPin(RIGHT_MOTOR_PIN_B);
#endif

    stop();   // never leave the pins in whatever state they powered up in
  }

  // Set both wheels at once. Values outside -255..255 are clamped, so you can
  // do arithmetic on speeds without worrying about overshooting.
  void set(int left, int right) {
    _left  = clamp(left);
    _right = clamp(right);

    // The REVERSED flags are applied here, at the very last moment, so that
    // _left and _right always mean what the outside world asked for. That is
    // what gets reported back in telemetry.
    apply(LEFT_MOTOR_PIN_A,  LEFT_MOTOR_PIN_B,  LEFT_MOTOR_REVERSED  ? -_left  : _left);
    apply(RIGHT_MOTOR_PIN_A, RIGHT_MOTOR_PIN_B, RIGHT_MOTOR_REVERSED ? -_right : _right);
  }

  void stop() { set(0, 0); }

  bool isMoving() const { return _left != 0 || _right != 0; }

  int left()  const { return _left; }
  int right() const { return _right; }

private:
  int _left  = 0;    // last speed asked for, -255..255
  int _right = 0;

  // Keeps a speed inside the range the driver board understands.
  static int clamp(int speed) {
    if (speed >  MOTOR_MAX_SPEED) return  MOTOR_MAX_SPEED;
    if (speed < -MOTOR_MAX_SPEED) return -MOTOR_MAX_SPEED;
    return speed;
  }

  // Hands one pin over to the PWM hardware and starts its counter.
  static void setupPwmPin(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);

    // frequency = system clock / (clkdiv * (wrap + 1)), so rearranged:
    float divider = (float)clock_get_hz(clk_sys)
                  / ((float)MOTOR_PWM_FREQ_HZ * (MOTOR_MAX_SPEED + 1));

    pwm_set_clkdiv(slice, divider);
    pwm_set_wrap(slice, MOTOR_MAX_SPEED);
    pwm_set_gpio_level(pin, 0);      // start stopped
    pwm_set_enabled(slice, true);
  }

  static void setupDigitalPin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
  }

  // Turns one signed speed into the two pin signals the driver board expects.
  //
  // This is the only place in the whole program that knows what kind of motor
  // driver is attached. The #if picks one version at COMPILE time, so the
  // unused one is not even built.
  static void apply(uint pinA, uint pinB, int speed) {
    int magnitude = speed < 0 ? -speed : speed;

#if MOTOR_DRIVER == MOTOR_DRIVER_DUAL_PWM
    // One pin per direction: drive the one you want and hold the other at
    // zero, both at zero = coast. This is what a bare H-bridge wants, so it
    // covers our L298N (IN1/IN2 and IN3/IN4) as well as a DRV8833 or TB6612.
    pwm_set_gpio_level(pinA, speed > 0 ? magnitude : 0);   // forward
    pwm_set_gpio_level(pinB, speed < 0 ? magnitude : 0);   // backward
#else
    // One pin says which way, the other says how fast. Only works on a board
    // that turns that single direction wire into the two opposite signals the
    // bridge needs, which means a board with an inverter on it: the old
    // Arduino robot's DRI0009 shield, and shields like it.
    gpio_put(pinB, speed < 0 ? 1 : 0);                     // direction
    pwm_set_gpio_level(pinA, magnitude);                   // speed
#endif
  }
};

// Try this:
//
//   1. Add turn(int speed, int steering) so that steering = 0 goes straight
//      and steering = +255 spins right on the spot. One line of arithmetic,
//      then call set().
//   2. Add a speed limit: a setMaxSpeed(int) that clamp() also respects, so a
//      nervous first test run cannot go full tilt.
//   3. Real motors do not start at the same speed. If your robot veers left
//      when told to go straight, add a per-wheel trim in Config.h and apply
//      it in set().
//   4. Set MOTOR_PWM_FREQ_HZ to 500 and listen. You can HEAR the pulse width
//      modulation, and the pitch is the frequency. Put it back afterwards.
