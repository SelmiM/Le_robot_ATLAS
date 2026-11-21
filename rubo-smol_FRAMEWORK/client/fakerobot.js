'use strict';

/**
 * Local HTTP simulator for the robot protocol and phone page.
 *
 * Serves robot/web/ and implements POST /api/command and GET /api/lines.
 * Commands, movement deadlines, telemetry and link timeout mirror the
 * firmware; distance readings are simulated.
 *
 *     node client/fakerobot.js [--http-port 8000] [--quiet]
 *     node client/remote.js localhost:8080
 */

const fs = require('fs');
const http = require('http');
const path = require('path');

// Match the defaults in robot/Config.h.
const DEFAULT_DURATION_MS = 1000;
const MAX_DURATION_MS = 5000;
const DEFAULT_TELEMETRY_MS = 200;
const MOTOR_MAX_SPEED = 255;
const WEB_TIMEOUT_MS = 1500;
const HTTP_PORT = 8080;          // the robot uses 80, which needs root here
const LINES_KEPT = 16;           // as in robot/WebRadio.h

const WEB_DIR = path.join(__dirname, '..', 'robot', 'web');

const started = Date.now();

/** Milliseconds since startup, matching nowMs() in Clock.h. */
function nowMs() {
  return Date.now() - started;
}

/**
 * Robot state and command handling, independent of HTTP.
 * update() enforces movement deadlines and link safety without blocking.
 */
class FakeRobot {
  constructor(verbose = true) {
    this.left = 0;
    this.right = 0;
    this.seq = -1;
    this.mission = 0;
    this.telemetryEvery = DEFAULT_TELEMETRY_MS;
    this.verbose = verbose;

    this._deadline = 0;
    this._hasDeadline = false;
    this._lastTelemetry = 0;
  }

  /** Set wheel speeds and arm a deadline, as in Robot::drive(). */
  drive(left, right, durationMs = DEFAULT_DURATION_MS) {
    const duration = Math.min(durationMs, MAX_DURATION_MS);
    this.left = clamp(left);
    this.right = clamp(right);
    this._deadline = nowMs() + duration;
    this._hasDeadline = true;
  }

  stop() {
    this.left = 0;
    this.right = 0;
    this._hasDeadline = false;
  }

  moving() { return this.left !== 0 || this.right !== 0; }

  /**
   * Apply a parsed command and return any response lines. Settings are applied
   * before stop, which cancels movement and the mission.
   */
  execute(command) {
    const out = [];

    if (command.seq !== undefined && command.seq !== null) {
      this.seq = asNumber(command.seq);
    }
    if (command.telemetry !== undefined && command.telemetry !== null) {
      let period = asNumber(command.telemetry);
      if (period < 0) period = 0;
      this.telemetryEvery = period;
    }
    if (command.mission !== undefined && command.mission !== null) {
      this.mission = asNumber(command.mission);
    }
    if (command.info) {
      out.push(JSON.stringify({ info: true, robot: 'faux-robot', missions: '', camera: false }));
    }

    if (command.stop) {
      this.mission = 0;
      this.stop();
      return out;
    }

    const hasLeft = command.left !== undefined && command.left !== null;
    const hasRight = command.right !== undefined && command.right !== null;
    if (!hasLeft && !hasRight) return out;

    const left = hasLeft ? asNumber(command.left) : 0;
    const right = hasRight ? asNumber(command.right) : 0;
    let duration = command.duration === undefined || command.duration === null
      ? DEFAULT_DURATION_MS
      : asNumber(command.duration);
    if (duration < 0) duration = DEFAULT_DURATION_MS;   // matches Robot::execute()

    this.drive(left, right, duration);
    if (this.verbose) {
      const held = Math.min(duration, MAX_DURATION_MS);
      console.log(`  wheels ${pad(this.left)} ${pad(this.right)}  for ${held} ms`);
    }
    return out;
  }

  /** Enforce safety and return telemetry lines due this update. */
  update(connected) {
    const out = [];

    if (this.moving()) {
      const timeIsUp = !this._hasDeadline || nowMs() >= this._deadline;
      if (timeIsUp || !connected) {
        this.stop();
        if (!connected) this.mission = 0;
        out.push(this.telemetry(true));
      }
    } else {
      this._hasDeadline = false;
    }

    if (this.telemetryEvery && nowMs() - this._lastTelemetry >= this.telemetryEvery) {
      out.push(this.telemetry(false));
    }

    return out;
  }

  telemetry(done = false) {
    this._lastTelemetry = nowMs();

    const message = {
      seq: this.seq,
      ms: nowMs(),
      left: this.left,
      right: this.right,
      moving: this.moving(),
      mission: this.mission,
      ...inventedDistance('front'),
      ...inventedDistance('side'),
    };
    if (done) message.done = true;

    return JSON.stringify(message);
  }
}

/**
 * Match JsonReader's command values: booleans become 1 or 0, and other
 * non-numbers become 0.
 */
function asNumber(value) {
  if (typeof value === 'number') return value;
  if (typeof value === 'boolean') return value ? 1 : 0;
  return 0;
}

function clamp(speed) {
  return Math.max(-MOTOR_MAX_SPEED, Math.min(MOTOR_MAX_SPEED, Math.trunc(speed)));
}

function pad(value) {
  return String(value).padStart(4);
}

/**
 * Simulate a wall at 500 mm with ±4 mm noise and a 2% read failure rate.
 */
function inventedDistance(name) {
  if (Math.random() < 0.02) {
    return { [`${name}_mm`]: -1, [`${name}_ok`]: false, [`${name}_q`]: -1 };
  }
  return {
    [`${name}_mm`]: 500 + Math.floor(Math.random() * 9) - 4,
    [`${name}_ok`]: true,
    [`${name}_q`]: 180 + Math.floor(Math.random() * 41) - 20,
  };
}

/** Parses one line the way the firmware does; returns [command] or [null, error line]. */
function parseCommand(line) {
  try {
    const command = JSON.parse(line);
    if (typeof command !== 'object' || command === null || Array.isArray(command)) {
      throw new Error('not an object');
    }
    return [command, null];
  } catch (error) {
    return [null, JSON.stringify({ error: 'that was not valid JSON' })];
  }
}

function serve(httpPort, verbose) {
  const robot = new FakeRobot(verbose);

  // Numbered outgoing lines, as in WebRadio.h.
  const kept = [];
  let lastId = 0;
  let webHeardAt = null;

  const send = (lines) => {
    for (const line of lines) {
      lastId += 1;
      kept.push({ id: lastId, text: line });
      if (kept.length > LINES_KEPT) kept.shift();
    }
  };

  const connected = () => webHeardAt !== null && nowMs() - webHeardAt < WEB_TIMEOUT_MS;

  const web = http.createServer((request, response) => {
    webHeardAt = nowMs();
    const url = new URL(request.url, 'http://robot');
    const plain = { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' };

    if (request.method === 'POST' && url.pathname === '/api/command') {
      let body = '';
      request.setEncoding('utf8');
      request.on('data', (chunk) => { body += chunk; });
      request.on('end', () => {
        const replies = [];
        for (const raw of body.split('\n')) {
          const line = raw.trim();
          if (!line) continue;
          const [command, error] = parseCommand(line);
          if (error) { replies.push(error); continue; }
          replies.push(...robot.execute(command));
        }
        send(replies);
        response.writeHead(204, { 'Cache-Control': 'no-store' });
        response.end();
      });
      return;
    }

    if (request.method === 'GET' && url.pathname === '/api/lines') {
      let after = Number(url.searchParams.get('after')) || 0;
      if (after > lastId) after = 0;          // the robot "restarted"
      const lines = kept.filter((line) => line.id > after).map((line) => line.text + '\n');
      response.writeHead(200, { ...plain, 'X-Last-Line': String(lastId) });
      response.end(lines.join(''));
      return;
    }

    if (request.method === 'GET') {
      const name = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
      const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };
      // Serve current files on every request.
      if (!name.includes('/') && !name.startsWith('.')) {
        fs.readFile(path.join(WEB_DIR, name), (error, data) => {
          if (error) {
            response.writeHead(404, plain);
            response.end('nothing here\n');
            return;
          }
          const type = types[path.extname(name)] || 'application/octet-stream';
          response.writeHead(200, { 'Content-Type': type + '; charset=utf-8',
                                    'Cache-Control': 'no-store' });
          response.end(data);
        });
        return;
      }
    }

    response.writeHead(404, plain);
    response.end('nothing here\n');
  });

  web.listen(httpPort, () => {
    console.log(`fake robot: phone page on http://localhost:${httpPort}/ -- ctrl-C to stop\n`);
  });

  // Advance simulated movement every 5 ms.
  setInterval(() => send(robot.update(connected())), 5);

  process.on('SIGINT', () => {
    console.log('\nstopped');
    process.exit(0);
  });
}

function main() {
  const args = process.argv.slice(2);
  const httpAt = args.indexOf('--http-port');
  const httpPort = httpAt >= 0 ? Number(args[httpAt + 1]) : HTTP_PORT;
  const verbose = !args.includes('--quiet');

  if (!Number.isFinite(httpPort) || httpPort <= 0) {
    console.error('usage: node client/fakerobot.js [--http-port N] [--quiet]');
    process.exit(1);
  }

  serve(httpPort, verbose);
}

if (require.main === module) main();

module.exports = { FakeRobot, nowMs };
