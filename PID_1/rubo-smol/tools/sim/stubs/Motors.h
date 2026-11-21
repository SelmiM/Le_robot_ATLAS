#pragma once
// Simulated motors: same interface as robot/Motors.h. The wheel speeds that
// result are worked out in tools/sim/SimWorld.h.
#include "SimWorld.h"
#include "Config.h"

class Motors {
public:
  void begin() { stop(); }

  void set(int left, int right) {
    _left  = clamp(left);
    _right = clamp(right);
    sim::leftCommand  = _left;
    sim::rightCommand = _right;
  }

  void stop() { set(0, 0); }

  bool isMoving() const { return _left != 0 || _right != 0; }

  int left()  const { return _left; }
  int right() const { return _right; }

  // Added to robot/Motors.h by the TP8 exercises MOT-1 and MOT-2, so that a
  // Robot.h using them still compiles here. MOT-3's trim is not simulated:
  // the model has its own imbalance (SIM_LEFT_STRENGTH).
  void turn(int speed, int steering) {
    set(speed - speed * steering / MOTOR_MAX_SPEED + steering,
        speed + speed * steering / MOTOR_MAX_SPEED - steering);
  }

  void setMaxSpeed(int limit) {
    if (limit < 0)               limit = 0;
    if (limit > MOTOR_MAX_SPEED) limit = MOTOR_MAX_SPEED;
    _maxSpeed = limit;
    set(_left, _right);
  }

  int maxSpeed() const { return _maxSpeed; }

  int clampToRange(int speed) const { return clamp(speed); }

private:
  int _left     = 0;
  int _right    = 0;
  int _maxSpeed = MOTOR_MAX_SPEED;

  int clamp(int speed) const {
    if (speed >  _maxSpeed) return  _maxSpeed;
    if (speed < -_maxSpeed) return -_maxSpeed;
    return speed;
  }
};
