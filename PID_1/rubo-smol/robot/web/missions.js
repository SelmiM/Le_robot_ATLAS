/**
 * Missions run on the phone, using polled telemetry from the robot.
 *
 * Each entry appears under "Missions sur le téléphone" and takes the page's
 * { speed, duration, distance } settings. Try edits with fakerobot.js at
 * http://localhost:8080/; reflash firmware to try them on the real robot.
 */

import { Mission } from './robot.js';

// MEASURE: how long a quarter turn takes at speed 180, on your floor.
const QUARTER_TURN_MS = 600;

export const missions = {
  /** One step: forward for `duration` ms. */
  avancer: ({ speed = 180, duration = 1000 } = {}) =>
    new Mission('avancer')
      .forward(speed, duration),

  /** Three steps: forward, stand still, come back. */
  'aller-retour': ({ speed = 180, duration = 1000 } = {}) =>
    new Mission('aller-retour')
      .forward(speed, duration)
      .pause(300)
      .backward(speed, duration),

  // PHONE-1. QUARTER_TURN_MS is measured on the floor, like the robot's own
  // in Config.h: turn four times and adjust until the robot faces home.
  'avancer-tourner': ({ speed = 180, duration = 1000 } = {}) =>
    new Mission('avancer-tourner')
      .forward(speed, duration)
      .turnRight(speed, QUARTER_TURN_MS),

  // PHONE-2. front_ok comes first: a failed reading is front_mm: -1, and
  // -1 < distance would stop the robot every time the sensor misses one.
  'avancer-obstacle': ({ speed = 160, duration = 1000, distance = 250 } = {}) =>
    new Mission('avancer-obstacle')
      .forward(speed, duration)
      .repeat(5)
      .stopWhen((t) => t.front_ok && t.front_mm < distance, 'obstacle devant'),

  // PHONE-3. repeat() comes before stopWhen(): it copies steps, and the
  // condition is not a step.
  carre: ({ speed = 180, duration = 1200, distance = 200 } = {}) =>
    new Mission('carre')
      .forward(speed, duration)
      .turnRight(speed, QUARTER_TURN_MS)
      .repeat(4)
      .stopWhen((t) => t.front_ok && t.front_mm < distance, 'quelque chose devant'),
};
