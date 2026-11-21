'use strict';

/**
 * Node client for the Pico robot, over HTTP or USB serial. A computer-side
 * tool for work beyond the labs. robot/web/robot.js is the browser
 * equivalent, with the same command methods.
 *
 * Use it as a library:
 *
 *     const { Robot } = require('./remote');
 *
 *     const bot = await Robot.open('192.168.1.42');
 *     const seq = bot.forward(200, 1500);
 *     await bot.waitUntilDone(seq);
 *     await bot.close();
 *
 * Run it directly for a demo:
 *
 *     node client/remote.js 192.168.1.42       // over WiFi
 *     node client/remote.js /dev/ttyACM0       // over the USB cable
 *     node client/remote.js localhost:8080     // against client/fakerobot.js
 *
 * Robot.open() selects the transport by address or serial path. HTTP uses
 * the routes in robot/WebRadio.h and Node's built-in fetch(); USB needs the
 * optional serialport dependency. The computer must share the robot's WiFi
 * network for HTTP access.
 */

const { EventEmitter } = require('events');

const HTTP_PORT = 80;
const POLL_MS = 100;       // how often to ask the robot for news, as the phone page does

/**
 * HTTP transport: sends lines, emits 'line' when one arrives, and emits
 * 'close' when the link goes away.
 */
class HttpTransport extends EventEmitter {
  constructor(base, after) {
    super();
    this.base = base;
    this._after = after;              // the number of the last line we have seen
    this._closed = false;
    this._sending = Promise.resolve();
    this._poll();
  }

  /** `target` is an address, with or without a port: '192.168.1.42', 'localhost:8080'. */
  static async connect(target, port = HTTP_PORT, timeoutMs = 5000) {
    const base = target.includes(':') ? `http://${target}` : `http://${target}:${port}`;

    // The first request checks that something answers, and says how far the
    // robot's numbering has got, so its older lines are not replayed to us.
    let response;
    try {
      response = await fetch(`${base}/api/lines`, { signal: AbortSignal.timeout(timeoutMs) });
      await response.text();
    } catch (error) {
      throw new Error(`no answer from ${base}: ${error.message}`);
    }
    return new HttpTransport(base, Number(response.headers.get('X-Last-Line')) || 0);
  }

  /**
   * Send commands in order. Sent side by side they could overtake each other
   * on the way, and a {"run":1} that arrives before its queue steps runs nothing.
   */
  write(line) {
    this._sending = this._sending
      .then(() => fetch(`${this.base}/api/command`, { method: 'POST', body: line + '\n' }))
      .catch(() => {});   // a lost command shows up as a "done" that never comes
  }

  /**
   * Poll for lines every POLL_MS: in HTTP only the client speaks first.
   * Polling also keeps the robot's connected() true. Stop asking and it stops
   * the wheels WEB_TIMEOUT_MS later.
   */
  async _poll() {
    while (!this._closed) {
      try {
        const response = await fetch(`${this.base}/api/lines?after=${this._after}`);
        const text = await response.text();
        this._after = Number(response.headers.get('X-Last-Line')) || this._after;
        for (const raw of text.split('\n')) {
          const line = raw.trim();
          if (line) this.emit('line', line);
        }
      } catch (error) {
        this.close();                 // the robot is gone
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, POLL_MS));
    }
  }

  close() {
    if (this._closed) return;
    this._closed = true;
    this.emit('close');
  }

  describe() { return this.base; }
}

/** USB serial transport with the same line and close events as HttpTransport. */
class SerialTransport extends EventEmitter {
  constructor(port) {
    super();
    this._port = port;
    this._buffer = '';

    this._port.on('data', (chunk) => {
      this._buffer += chunk.toString('utf8');
      let cut;
      while ((cut = this._buffer.indexOf('\n')) >= 0) {
        const line = this._buffer.slice(0, cut).trim();
        this._buffer = this._buffer.slice(cut + 1);

        // The cable carries the robot's telemetry AND every printf() in the
        // firmware -- the startup banner, and anything you add while
        // debugging. One wire, two jobs. Messages are JSON objects, so a line
        // that does not start with '{' is not for us.
        //
        // This is a deliberate compromise, not an oversight: see the TRY THIS
        // at the bottom of robot/Radio.h for what doing it properly costs.
        if (!line) continue;
        if (!line.startsWith('{')) { this.emit('noise', line); continue; }
        this.emit('line', line);
      }
    });

    this._port.on('close', () => this.emit('close'));
    this._port.on('error', (error) => this.emit('error', error));
  }

  static connect(path, baudRate = 115200) {
    let SerialPort;
    try {
      ({ SerialPort } = require('serialport'));
    } catch (error) {
      throw new Error(
        `talking over the cable needs the serialport package:\n` +
        `    npm install serialport\n` +
        `(or use the robot's IP address instead, once RADIO is RADIO_WIFI)`
      );
    }

    return new Promise((resolve, reject) => {
      const port = new SerialPort({ path, baudRate }, (error) => {
        if (error) reject(error);
        else resolve(new SerialTransport(port));
      });
    });
  }

  write(line) { this._port.write(line + '\n'); }
  close() { this._port.close(); }
  describe() { return this._port.path; }
}

/** Node client for robot commands and telemetry over either transport. */
class Robot extends EventEmitter {
  constructor(transport) {
    super();
    this._transport = transport;
    // Start the numbering somewhere random, so that telemetry still carrying
    // the seq of an earlier session cannot be taken for the answer to ours.
    this._seq = Math.floor(Math.random() * 1e9);
    this._waiting = [];        // read() calls with nothing to hand them yet
    this._pending = [];        // messages that arrived with nobody waiting
    this.last = null;          // the most recent telemetry, always up to date

    transport.on('line', (line) => {
      let message;
      try {
        message = JSON.parse(line);
      } catch (error) {
        return;                // a half-line, or noise on the cable
      }

      // {"error": ...} is the robot refusing one line. The link is fine, so
      // it must not end the program, and an 'error' event with no listener
      // does exactly that (see transport.on('error') below). It is raised
      // only for code that listens; read() hands it out either way.
      if (message.error) {
        if (this.listenerCount('error')) this.emit('error', new Error(message.error));
      } else {
        this.last = message;
      }

      this.emit('message', message);

      const waiter = this._waiting.shift();
      if (waiter) { clearTimeout(waiter.timer); waiter.resolve(message); }
      else this._pending.push(message);

      // Telemetry arrives several times a second. If nobody is reading, the
      // backlog is not a queue worth keeping -- it is just old news.
      if (this._pending.length > 200) this._pending.splice(0, 100);
    });

    transport.on('close', () => {
      this.emit('close');
      for (const waiter of this._waiting) {
        clearTimeout(waiter.timer);
        waiter.resolve(null);
      }
      this._waiting = [];
    });

    // A hard disconnect raises 'error' before 'close'. With no listener here
    // the EventEmitter throws and the whole program dies -- a reflash while
    // this was connected would take it down. Treat it as a close: the waiters
    // get their null, and the robot's own safety rules stop the wheels on its
    // end of the wire.
    transport.on('error', () => this.emit('close'));
  }

  /**
   * Open a connection. Give it either:
   *
   *     Robot.open('192.168.1.42')     an address  -> WiFi
   *     Robot.open('localhost:8080')   the same, on another port
   *     Robot.open('/dev/ttyACM0')     a device    -> the USB cable
   *
   * A target starting with '/' or 'COM' is treated as a serial port.
   */
  static async open(target, options = {}) {
    const serial = target.startsWith('/') || /^COM\d+$/i.test(target);
    const transport = serial
      ? await SerialTransport.connect(target, options.baudRate)
      : await HttpTransport.connect(target, options.port, options.timeoutMs);

    const bot = new Robot(transport);
    bot.base = serial ? null : transport.base;   // snapshots are HTTP, so WiFi only
    return bot;
  }

  /** Send one command. Returns the sequence number it was tagged with. */
  send(command) {
    this._seq += 1;
    if (command.seq === undefined) command.seq = this._seq;
    this._transport.write(JSON.stringify(command));
    return command.seq;
  }

  /**
   * Wheel speeds from -255 to 255, for `duration` milliseconds.
   * Positive is forward, negative is backward, 0 is stopped.
   *
   * The robot caps `duration` at MAX_DURATION_MS (5 s by default, see
   * Config.h) and stops the wheels on its own when it runs out. Asking for
   * longer is not an error; you just get 5 seconds. To keep moving, send
   * another command -- that is deliberate, and it is what stops the robot if
   * this program crashes.
   */
  drive(left, right, duration = 1000) {
    return this.send({ left, right, duration });
  }

  forward(speed = 200, duration = 1000) { return this.drive(speed, speed, duration); }
  backward(speed = 200, duration = 1000) { return this.drive(-speed, -speed, duration); }
  turnLeft(speed = 200, duration = 500) { return this.drive(-speed, speed, duration); }
  turnRight(speed = 200, duration = 500) { return this.drive(speed, -speed, duration); }

  stop() { return this.send({ stop: true }); }

  /**
   * Switch one of the robot's own behaviours on, or 0 for off.
   *
   * A command tells the robot what to do once; a mission is the robot deciding
   * for itself until you stop it. See Robot::mission() in robot/Robot.h --
   * that is where they are written.
   */
  mission(number) { return this.send({ mission: number }); }

  /** How often the robot reports back, in ms. 0 turns it off. */
  telemetry(periodMs) { return this.send({ telemetry: periodMs }); }

  /** The next message from the robot, or null if none arrives in time. */
  read(timeoutMs = 2000) {
    if (this._pending.length) return Promise.resolve(this._pending.shift());

    return new Promise((resolve) => {
      const waiter = { resolve, timer: null };
      waiter.timer = setTimeout(() => {
        const at = this._waiting.indexOf(waiter);
        if (at >= 0) this._waiting.splice(at, 1);
        resolve(null);
      }, timeoutMs);
      this._waiting.push(waiter);
    });
  }

  /**
   * Wait for the next message for which `test(message)` is true. Resolves to
   * that message, or null when `timeoutMs` runs out or `aborted()` returns
   * true. Only messages arriving AFTER the call count; read() is unaffected.
   *
   * This is how a stop condition is written:
   *
   *     await bot.waitUntil(t => t.front_ok && t.front_mm < 200);
   */
  waitUntil(test, timeoutMs = 10000, aborted = null) {
    return new Promise((resolve) => {
      const finish = (value) => {
        this.off('message', listener);
        this.off('close', onClose);
        clearTimeout(timer);
        clearInterval(check);
        resolve(value);
      };
      const listener = (message) => { if (test(message)) finish(message); };
      const onClose = () => finish(null);
      const timer = setTimeout(() => finish(null), timeoutMs);
      const check = setInterval(() => { if (aborted && aborted()) finish(null); }, 10);
      this.on('message', listener);
      this.on('close', onClose);
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

  /**
   * Fetch a picture. Resolves to { width, height, pixels }, where pixels is a
   * Buffer with one byte of grey per pixel, top row first.
   *
   * Only over WiFi: the pictures are served over HTTP, and the cable has no
   * HTTP on it.
   */
  async snapshot() {
    if (!this.base) throw new Error('snapshots need WiFi -- there is no HTTP over the cable');

    const response = await fetch(`${this.base}/snapshot.pgm`);
    if (!response.ok) throw new Error(`the robot said ${response.status}`);
    return parsePgm(Buffer.from(await response.arrayBuffer()));
  }

  async close() {
    try { this.stop(); } catch (error) { /* already gone */ }
    await new Promise((resolve) => setTimeout(resolve, 50));  // let it go out
    this._transport.close();
  }
}

/** Minimal binary PGM (P5) reader, so you need no image library. */
function parsePgm(raw) {
  const fields = [];
  let at = 0;

  while (fields.length < 4) {
    while (at < raw.length && /\s/.test(String.fromCharCode(raw[at]))) at += 1;
    if (raw[at] === 0x23) { at = raw.indexOf(0x0a, at) + 1; continue; }   // '#'
    const start = at;
    while (at < raw.length && !/\s/.test(String.fromCharCode(raw[at]))) at += 1;
    fields.push(raw.slice(start, at).toString('ascii'));
  }
  at += 1;                                        // the single byte after the header

  if (fields[0] !== 'P5') throw new Error(`expected a P5 image, got ${fields[0]}`);
  const width = Number(fields[1]);
  const height = Number(fields[2]);
  return { width, height, pixels: raw.slice(at, at + width * height) };
}

// Run directly to check the connection and basic commands.
async function demo(target) {
  console.log(`connecting to ${target} ...`);
  const bot = await Robot.open(target);
  console.log(`connected: ${bot._transport.describe()}\n`);

  bot.on('error', (error) => console.log(`  the robot said: ${error.message}`));

  bot.telemetry(200);

  console.log('forward');
  console.log(' ', await bot.waitUntilDone(bot.forward(180, 1000)));

  console.log('turning');
  await bot.waitUntilDone(bot.turnRight(180, 600));

  console.log('\ndistance readings:');
  for (let i = 0; i < 5; i += 1) {
    const message = await bot.read();
    if (message) {
      console.log(`   front=${message.front_mm} mm  ok=${message.front_ok}` +
                  `  signal=${message.front_q}`);
    }
  }

  try {
    const { width, height, pixels } = await bot.snapshot();
    const average = pixels.reduce((sum, value) => sum + value, 0) / pixels.length;
    console.log(`\npicture: ${width}x${height}, average brightness ${average.toFixed(0)}`);
  } catch (error) {
    console.log(`\nno picture: ${error.message}`);
  }

  await bot.close();
  console.log('\ndone');
}

if (require.main === module) {
  const target = process.argv[2];
  if (!target) {
    console.error(`usage: node ${process.argv[1]} <robot-ip | host:port | /dev/ttyACM0>`);
    process.exit(1);
  }
  demo(target).catch((error) => {
    console.error(error.message);
    process.exit(1);
  });
}

module.exports = { Robot, HttpTransport, SerialTransport, parsePgm };
