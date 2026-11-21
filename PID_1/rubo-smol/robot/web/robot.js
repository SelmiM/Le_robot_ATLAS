/**
 * Browser client for the robot and the Mission class used by missions.js.
 * See WebRadio.h for the HTTP routes; client/remote.js is the Node equivalent.
 *
 *     import { Robot, Mission } from './robot.js';
 *
 *     const bot = new Robot();
 *     bot.start();
 *     const seq = bot.forward(200, 1500);
 *     await bot.waitUntilDone(seq);
 *
 * This file is embedded in firmware: edits need a rebuild and reflash.
 * bot.latency.request measures an HTTP round trip; bot.latency.loop includes
 * the waits for telemetry and the next poll. Phone missions make decisions
 * using those delayed readings; firmware missions read sensors locally.
 */

export const POLL_MS = 100;         // how often to ask the robot for news
const LINK_LOST_MS = 1000;          // no answer for this long: show the link as down
const SEND_RETRIES = 3;             // a command that did not get through is sent again
const SEND_RETRY_MS = 100;
const SEND_TIMEOUT_MS = 1000;       // a command not answered by then is sent again
const POLL_TIMEOUT_MS = 2000;       // a poll not answered by then is given up

// A fetch() has no deadline of its own: on a connection the robot dropped, it
// can wait for minutes, and the loop that awaits it waits too. This signal
// cancels it after `ms`.
function deadline(ms) {
  const controller = new AbortController();
  setTimeout(() => controller.abort(), ms);
  return controller.signal;
}

/** A few numbers about a delay: the last one, the average, the worst. */
export class Stats {
  constructor(keep = 50) {
    this.keep = keep;
    this.values = [];
  }

  add(ms) {
    this.values.push(ms);
    if (this.values.length > this.keep) this.values.shift();
  }

  get last() { return this.values.length ? this.values[this.values.length - 1] : null; }
  get mean() {
    if (!this.values.length) return null;
    return this.values.reduce((sum, v) => sum + v, 0) / this.values.length;
  }
  get max() { return this.values.length ? Math.max(...this.values) : null; }
}

/** Browser connection to the robot, with command and telemetry helpers. */
export class Robot {
  constructor(base = '') {
    this.base = base;              // '' = the robot that sent this page
    this.last = null;              // the most recent telemetry
    this.info = null;              // the answer to {"info": true}
    this.latency = { request: new Stats(), loop: new Stats() };

    // Start the numbering somewhere random, so that telemetry still carrying
    // the seq of a command sent before the page was reloaded cannot be taken
    // for the answer to one of ours. The robot keeps seq in an int.
    this._seq = Math.floor(Math.random() * 1e9);
    this._after = 0;              // the number of the last line we have seen
    this._listeners = new Set();
    this._sentAt = new Map();      // seq -> when it was sent, for latency.loop
    this._lastAnswer = 0;
    this._running = false;
  }

  /** Start asking the robot for news, every POLL_MS. */
  start() {
    if (this._running) return;
    this._running = true;
    this._poll();
  }

  /** Stop asking. The robot stops on its own WEB_TIMEOUT_MS later. */
  pause() { this._running = false; }

  /** True while the robot is answering. */
  get connected() {
    return performance.now() - this._lastAnswer < LINK_LOST_MS;
  }

  /** Call `listener(message)` for every line the robot sends. */
  on(listener) { this._listeners.add(listener); }
  off(listener) { this._listeners.delete(listener); }

  /** Send one command. Returns the sequence number it was tagged with. */
  send(command) {
    this._seq += 1;
    if (command.seq === undefined) command.seq = this._seq;
    const seq = command.seq;

    const sentAt = performance.now();
    this._sentAt.set(seq, sentAt);
    if (this._sentAt.size > 100) this._sentAt.delete(this._sentAt.keys().next().value);

    // A request that never reached the robot is sent again. WiFi drops the
    // odd one, and the robot refuses new connections while all of its slots
    // are busy (see WebRadio.h): two phones loading the page at once is
    // enough. Sending a movement or a stop twice is harmless; losing a stop
    // is not.
    const body = JSON.stringify(command) + '\n';
    const attempt = (triesLeft) => {
      fetch(this.base + '/api/command',
            { method: 'POST', body, signal: deadline(SEND_TIMEOUT_MS) }).then((response) => {
        this.latency.request.add(performance.now() - sentAt);
        if (!response.ok) this._emit({ error: `the robot answered ${response.status}` });
      }).catch(() => {
        if (triesLeft > 0) setTimeout(() => attempt(triesLeft - 1), SEND_RETRY_MS);
        else this._emit({ error: 'command lost: the robot did not answer' });
      });
    };
    attempt(SEND_RETRIES);

    return seq;
  }

  /**
   * Wheel speeds from -255 to 255, for `duration` milliseconds. The robot
   * stops on its own when the time is up, whatever happens to this page.
   */
  drive(left, right, duration = 1000) { return this.send({ left, right, duration }); }

  forward(speed = 200, duration = 1000) { return this.drive(speed, speed, duration); }
  backward(speed = 200, duration = 1000) { return this.drive(-speed, -speed, duration); }
  turnLeft(speed = 200, duration = 500) { return this.drive(-speed, speed, duration); }
  turnRight(speed = 200, duration = 500) { return this.drive(speed, -speed, duration); }

  stop() { return this.send({ stop: true }); }

  /** One of the robot's own missions, from Robot::mission(). 0 is off. */
  mission(number) { return this.send({ mission: number }); }

  /** How often the robot reports back, in ms. */
  telemetry(periodMs) { return this.send({ telemetry: periodMs }); }

  /**
   * Wait for the next message for which `test(message)` is true. Resolves to
   * that message, or null when `timeoutMs` runs out or `aborted()` returns
   * true. Only messages arriving AFTER the call count.
   */
  waitUntil(test, timeoutMs = 10000, aborted = null) {
    return new Promise((resolve) => {
      const finish = (value) => {
        this.off(listener);
        clearTimeout(timer);
        clearInterval(check);
        resolve(value);
      };
      const listener = (message) => { if (test(message)) finish(message); };
      const timer = setTimeout(() => finish(null), timeoutMs);
      const check = setInterval(() => { if (aborted && aborted()) finish(null); }, 10);
      this.on(listener);
    });
  }

  /**
   * Wait for the robot to say the movement tagged `seq` finished. Use this
   * rather than sleeping for the duration you asked for: it asks the robot.
   */
  waitUntilDone(seq, timeoutMs = 6000, aborted = null) {
    // >= rather than ===: the robot reports the seq of the LAST command it
    // got, so a {"telemetry": ...} sent after the movement changes it.
    return this.waitUntil((m) => m.done && m.seq >= seq, timeoutMs, aborted);
  }

  async _poll() {
    while (this._running) {
      const started = performance.now();
      try {
        const response = await fetch(`${this.base}/api/lines?after=${this._after}`,
                                      { cache: 'no-store', signal: deadline(POLL_TIMEOUT_MS) });
        const lastLine = Number(response.headers.get('X-Last-Line'));
        const text = await response.text();

        // The robot numbers its lines from 1 when it starts. A number going
        // backwards means it restarted: it has forgotten our settings.
        if (lastLine < this._after) this._emit({ restarted: true });
        this._after = lastLine;
        this._lastAnswer = performance.now();

        for (const line of text.split('\n')) {
          if (!line) continue;
          try { this._receive(JSON.parse(line)); } catch (error) { /* half a line */ }
        }
      } catch (error) {
        // No answer. connected goes false by itself after LINK_LOST_MS.
      }

      const spent = performance.now() - started;
      await sleep(Math.max(10, POLL_MS - spent));
    }
  }

  _receive(message) {
    if (message.info) this.info = message;
    else if (message.ms !== undefined) {
      this.last = message;

      // The first line that carries back the seq of a command we sent is
      // the moment we KNOW the robot got it.
      const sentAt = this._sentAt.get(message.seq);
      if (sentAt !== undefined) {
        this.latency.loop.add(performance.now() - sentAt);
        this._sentAt.delete(message.seq);
      }
    }
    this._emit(message);
  }

  _emit(message) {
    for (const listener of [...this._listeners]) listener(message);
  }
}

/**
 * Sequences of commands, with a condition that can cut them short.
 *
 *     new Mission('carre')
 *       .forward(180, 1000)
 *       .turnRight(180, 600)
 *       .repeat(4)
 *       .stopWhen(t => t.front_ok && t.front_mm < 200, 'quelque chose devant');
 */
export class Mission {
  constructor(name) {
    this.name = name;
    this.steps = [];
    this._condition = null;
    this._reason = 'condition d\'arrêt atteinte';
  }

  drive(left, right, duration = 1000) {
    this.steps.push({ kind: 'drive', left, right, duration });
    return this;
  }

  forward(speed = 200, duration = 1000) { return this.drive(speed, speed, duration); }
  backward(speed = 200, duration = 1000) { return this.drive(-speed, -speed, duration); }
  turnLeft(speed = 200, duration = 500) { return this.drive(-speed, speed, duration); }
  turnRight(speed = 200, duration = 500) { return this.drive(speed, -speed, duration); }

  /** Sit still for a while, without ending the mission. */
  pause(duration = 500) {
    this.steps.push({ kind: 'pause', duration });
    return this;
  }

  /** Repeat everything built so far, `times` times in total. */
  repeat(times) {
    const once = this.steps.slice();
    for (let i = 1; i < times; i += 1) this.steps.push(...once.map((s) => ({ ...s })));
    return this;
  }

  /**
   * Give up early when `test(telemetry)` becomes true.
   *
   *     .stopWhen(t => t.front_ok && t.front_mm < 250, 'mur devant')
   *
   * `t` is the latest telemetry received by the phone, not a live sensor read.
   */
  stopWhen(test, reason = 'condition d\'arrêt atteinte') {
    this._condition = test;
    this._reason = reason;
    return this;
  }

  /**
   * Run it. Resolves to { completed, stoppedAt, reason }. The robot is always
   * left stopped.
   *
   * `log(text)` is told about every step, with how long it really took.
   * `aborted()` is checked all along: return true to end the mission now.
   */
  async run(bot, { log = console.log, aborted = () => false } = {}) {
    log(`mission « ${this.name} » : ${this.steps.length} étapes`);
    bot.telemetry(100);   // stop conditions only see the telemetry we receive

    let triggered = null;
    const watch = (message) => {
      if (!triggered && this._condition && message.ms !== undefined && this._condition(message)) {
        triggered = message;
      }
    };
    bot.on(watch);
    const interrupted = () => triggered !== null || aborted();
    const started = performance.now();

    try {
      for (let i = 0; i < this.steps.length; i += 1) {
        if (aborted()) {
          log(`  interrompue à l'étape ${i + 1}`);
          return { completed: false, stoppedAt: i, reason: 'interrompue' };
        }
        if (triggered) {
          log(`  arrêt à l'étape ${i + 1} : ${this._reason}`);
          return { completed: false, stoppedAt: i, reason: this._reason };
        }

        const step = this.steps[i];
        const stepStarted = performance.now();

        if (step.kind === 'pause') {
          bot.stop();
          await sleepUnless(step.duration, interrupted);
          log(`  ${i + 1}/${this.steps.length} ${describe(step)}`);
          continue;
        }

        const seq = bot.drive(step.left, step.right, step.duration);
        const done = await bot.waitUntilDone(seq, step.duration + 2000, interrupted);
        const took = Math.round(performance.now() - stepStarted);

        if (!done) {
          const reason = triggered ? this._reason
                       : aborted() ? 'interrompue'
                       : 'le robot ne répond plus';
          log(`  ${i + 1}/${this.steps.length} ${describe(step)} : ${reason} après ${took} ms`);
          return { completed: false, stoppedAt: i, reason };
        }

        // Asked for step.duration; the phone saw "done" `took` ms after
        // sending. The difference is the round trip, as the phone sees it.
        log(`  ${i + 1}/${this.steps.length} ${describe(step)} : vu fini après ${took} ms`);
      }

      log(`  terminée en ${Math.round(performance.now() - started)} ms`);
      return { completed: true, stoppedAt: null, reason: null };
    } finally {
      bot.off(watch);
      bot.stop();
    }
  }
}

function describe(step) {
  if (step.kind === 'pause') return `pause ${step.duration} ms`;
  return `roues ${step.left} ${step.right} pendant ${step.duration} ms`;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function sleepUnless(ms, interrupted) {
  const end = performance.now() + ms;
  while (performance.now() < end && !interrupted()) await sleep(Math.min(20, end - performance.now()));
}
