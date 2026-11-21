/**
 * Missions run on the phone, using polled telemetry from the robot.
 *
 * Each entry appears under "Missions sur le téléphone" and takes the page's
 * { speed, duration, distance } settings. Try edits with fakerobot.js at
 * http://localhost:8080/; reflash firmware to try them on the real robot.
 */

import { Mission } from './robot.js';

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

    'avancer-obstacle': ({ speed = 160, duration = 1000, distance = 250 } = {}) =>
    new Mission('avancer-obstacle')
    .forward(speed, duration).repeat(5)
    .stopWhen((t) => t.front_ok && t.front_mm < distance, 'obstacle devant'),
    
    'avancer-tourner': ({ speed = 180, duration = 1000, distance = 250 } = {}) =>
    new Mission('avancer-tourner')
      .forward(speed, duration)
      .turnRight(speed, 2900)
      
      .stopWhen(
        (t) => t.front_ok && t.front_mm < distance,
        'obstacle détecté'
      ),
  /** Dessiner un carré : avancer puis tourner à droite quatre fois. */
  'carre': ({ speed = 180, duration = 1000, distance = 250 } = {}) =>
    new Mission('carre')
      .forward(speed, duration)
      .turnRight(speed, 2900)
      .repeat(4)
      .stopWhen(
        (t) => t.front_ok && t.front_mm < distance,
        'obstacle détecté'
      ),
        
};


// Try this:
//
//  Add each one to `missions` above, with the name given. Try it against the
//  fake robot first: it costs a reload, not a reflash.
//
//   1. 'avancer-tourner': forward for `duration`, then a quarter turn to the
//      right. How long is a quarter turn? Measure it on the floor: the fake
//      robot cannot tell you.
//
//   2. 'avancer-obstacle': forward in five steps of `duration`, stopping as
//      soon as the front sensor reads less than `distance`. Use stopWhen().
//      Then read your condition again: what does it do when the sensor
//      reports front_mm: -1?
//
//      The fake robot's wall is always about 500 mm away, so set the page's
//      "arrêt à" slider above 500 to see the stop happen.
//
//   3. 'carre': four times (forward, quarter turn), cut short if something
//      is in the way. repeat() copies every step built so far.


