#pragma once
#include <cstdint>
#include <cstdio>

#include "Config.h"
#include "Clock.h"
#include "Motors.h"
#include "UltrasonicSensor.h"
#include "TofSensor.h"
#include "Display.h"
#include "Camera.h"
#include "Radio.h"
#include "Json.h"

/**
 * Robot handles commands, missions, movement deadlines and telemetry.
 * Add autonomous behaviour in mission() below.
 *
 * Commands are JSON objects, one per line (USB or WiFi):
 *
 *     {"left": 200, "right": 200}              drive forward for 1 second
 *     {"left": 200, "right": -200}             spin on the spot
 *     {"left": 200, "right": 200, "duration": 3000}   ... for 3 seconds
 *     {"stop": true}                           stop now, and cancel any mission
 *     {"mission": 1}                           run mission 1; 0 turns it off
 *     {"telemetry": 500}                       send sensor data every 500 ms
 *     {"telemetry": 0}                         stop sending sensor data
 *     {"info": true}                           the robot's name and missions
 *     {"seq": 7, "left": 100, "right": 100}    tag it, and get the tag back
 *
 * "seq" tags a command and is returned in its replies. A reply with
 * "done":true means the movement has ended.
 *
 * Telemetry is also one JSON object per line:
 *
 *     {"seq":7,"ms":41230,"left":100,"right":100,"moving":true,"mission":0,
 *      "front_mm":412,"front_ok":true,"front_q":-1,
 *      "side_mm":-1,"side_ok":false,"side_q":-1}
 *
 * To add a command, handle it in execute(), using has() to distinguish a
 * missing field from zero. Add a client method if needed. Do not wait in
 * execute(): it runs in the same loop as the safety check.
 */
class Robot
{
public:
    Robot(Radio &radio) : _radio(radio) {}

    // main.cpp reads these to report device status at startup.
    Motors motors;
    UltrasonicSensor front;
    TofSensor side;
    Display display;
    Camera camera;

    // Every device checks its own *_ENABLED in Config.h, so each begin() below
    // does nothing for a device that is switched off.
    void begin()
    {
        motors.begin(); // first, so the wheels are known to be off
        front.begin();
        side.begin();
        display.begin();
        camera.begin();
    }

    // Sensors first, then commands and missions, then the safety check.
    // Keep this loop non-blocking so movement deadlines are checked promptly.
    void update()
    {
        front.update();
        side.update();

        readCommands();
        mission();
        enforceSafety();
        sendTelemetryIfDue();
        refreshDisplayIfDue();
    }

    // Use drive() for every movement. Unlike motors.set(), it arms the deadline
    // checked by enforceSafety(). Direct calls to motors.set() are stopped as a
    // fault on the next pass through the loop.
    void drive(int left, int right, uint32_t durationMs = DEFAULT_DURATION_MS)
    {
        if (durationMs > MAX_DURATION_MS)
            durationMs = MAX_DURATION_MS;

        motors.set(left, right);
        _moveDeadline = nowMs() + durationMs;
        _hasDeadline = true;
    }

private:
    Radio &_radio;

    static constexpr size_t MAX_LINE = Radio::MAX_LINE;

    int _seq = -1;
    int _mission = 0; // 0 = no mission
    uint32_t _moveDeadline = 0;
    bool _hasDeadline = false;
    uint32_t _telemetryEvery = DEFAULT_TELEMETRY_MS;
    uint32_t _lastTelemetry = 0;
    uint32_t _lastDisplay = 0;
    // Keep the WiFi address on screen until the first command arrives.
    bool _showBanner = (RADIO == RADIO_WIFI);

    // Add missions here. A mission must return immediately on every call;
    // use nowMs() to track time and drive() to move safely. {"mission": 0} or
    // {"stop": true} turns it off. Give each mission a comma-separated name
    // in number order so the phone page can show its button:
    //
    //      static constexpr const char* MISSION_NAMES = "mur,carre";
    static constexpr const char *MISSION_NAMES = "obstacle";
    void stopAtObstacle() 
    {
        if (!front.valid())
            return; // ne pas utiliser une mesure absente
        if (front.distanceMm() > 150)
            return;
        if (!motors.isMoving()) 
            return;
            
        if(motors.left()>0 && motors.right()>0){
            motors.stop(); 
        }
         
      
       
        // À compléter : si aucune roue n'avance, retourner.
        // À compléter : arrêter le mouvement et signaler sa fin.
    }
    void mission()
    {
        switch (_mission)
        {
        case 0:
            //break;
        case 1:
            stopAtObstacle();
            break;

            // case 1: followWall(); break;
            // case 2: driveSquare(); break;

        default:
            break;
        }
    }

    // Drain all complete lines so a burst does not linger in the input buffer.
    void readCommands()
    {
        char line[MAX_LINE];

        while (_radio.receive(line, sizeof(line)))
        {
            JsonReader command;

            if (!command.parse(line))
            {
                // Reject malformed or truncated input before changing robot state.
                sendError("that was not valid JSON");
                continue;
            }
            execute(command);
        }
    }

    // A command can update several settings at once, including wheel speeds.
    void execute(const JsonReader &command)
    {
        // Any command, including telemetry settings, replaces the WiFi banner.
        _showBanner = false;

        if (command.has("seq"))
            _seq = (int)command.integer("seq");

        if (command.has("telemetry"))
        {
            // Zero or a negative value disables periodic telemetry.
            long period = command.integer("telemetry");
            _telemetryEvery = (period > 0) ? (uint32_t)period : 0;
        }

        if (command.has("mission"))
            _mission = (int)command.integer("mission");

        if (command.boolean("info"))
            sendInfo();

        // Stop takes precedence over movement in the same object.
        if (command.boolean("stop"))
        {
            _mission = 0;
            motors.stop();
            _hasDeadline = false;
            return;
        }

        // Settings-only commands do not change wheel speeds.
        bool hasLeft = command.has("left");
        bool hasRight = command.has("right");
        if (!hasLeft && !hasRight)
            return;

        // An omitted wheel in a movement command has speed zero.
        int left = hasLeft ? (int)command.integer("left") : 0;
        int right = hasRight ? (int)command.integer("right") : 0;

        long askedFor = command.has("duration")
                            ? command.integer("duration")
                            : DEFAULT_DURATION_MS;
        // Preserve zero; default negative durations before casting to unsigned.
        uint32_t duration = (askedFor >= 0) ? (uint32_t)askedFor : DEFAULT_DURATION_MS;

        // drive() applies MAX_DURATION_MS.
        drive(left, right, duration);
    }

    // Stop when the deadline passes, when there is no armed deadline, or when
    // the link is lost. A lost link also cancels the mission so it cannot
    // restart movement on the next pass.
    void enforceSafety()
    {
        if (!motors.isMoving())
        {
            _hasDeadline = false;
            return;
        }

        // Signed subtraction handles the 32-bit clock wrapping; see Clock.h.
        bool timeIsUp = !_hasDeadline || (int32_t)(nowMs() - _moveDeadline) >= 0;
        bool lostTheComputer = !_radio.connected();

        if (timeIsUp || lostTheComputer)
        {
            motors.stop();
            _hasDeadline = false;
            // Do not let a mission re-arm the wheels after a disconnect.
            if (lostTheComputer)
                _mission = 0;
            sendTelemetry(true);
        }
    }

    void sendTelemetryIfDue()
    {
        if (_telemetryEvery == 0)
            return; // reporting turned off
        if (nowMs() - _lastTelemetry < _telemetryEvery)
            return; // not yet
        sendTelemetry(false);
    }

    // Send one telemetry line; the radio drops it if no client is listening.
    void sendTelemetry(bool done)
    {
        char line[MAX_LINE];
        JsonWriter out(line, sizeof(line));

        out.integer("seq", _seq);
        out.integer("ms", nowMs()); // milliseconds since the robot booted
        out.integer("left", motors.left());
        out.integer("right", motors.right());
        out.boolean("moving", motors.isMoving());
        out.integer("mission", _mission);

        // The front sensor has no quality measurement, so front_q is always -1.
        out.integer("front_mm", front.distanceMm());
        out.boolean("front_ok", front.valid());
        out.integer("front_q", front.quality());
        out.integer("side_mm", side.distanceMm());
        out.boolean("side_ok", side.valid());
        out.integer("side_q", side.quality());

        if (done)
            out.boolean("done", true);

        _radio.send(out.finish());
        _lastTelemetry = nowMs();
    }

    // Display writes block briefly on I2C. Limit refreshes and send only rows
    // whose text has changed; keep the WiFi address visible until a command.
    void refreshDisplayIfDue()
    {
        if (!display.connected())
            return;
        if (_showBanner)
            return;
        if (nowMs() - _lastDisplay < DISPLAY_REFRESH_MS)
            return;
        _lastDisplay = nowMs();

        char row[Display::COLUMNS + 1];

        snprintf(row, sizeof(row), "L%+4d  R%+4d", motors.left(), motors.right());
        if (!display.alreadyShowing(0, row))
            display.line(0, row);

        // Show missing readings as ---- rather than a numeric distance.
        char frontMm[12] = "----", sideMm[12] = "----";
        if (front.valid())
            snprintf(frontMm, sizeof(frontMm), "%4d", front.distanceMm());
        if (side.valid())
            snprintf(sideMm, sizeof(sideMm), "%4d", side.distanceMm());
        snprintf(row, sizeof(row), "F%s S%s  M%d", frontMm, sideMm, _mission);
        if (!display.alreadyShowing(1, row))
            display.line(1, row);
    }

    // The phone page uses this to display the robot name and mission buttons.
    //
    //     {"info":true,"robot":"robot","missions":"mur,carre","camera":false}
    void sendInfo()
    {
        char line[MAX_LINE];
        JsonWriter out(line, sizeof(line));
        out.boolean("info", true);
        out.text("robot", ROBOT_NAME);
        out.text("missions", MISSION_NAMES);
        out.boolean("camera", camera.connected());
        _radio.send(out.finish());
    }

    void sendError(const char *message)
    {
        char line[MAX_LINE];
        JsonWriter out(line, sizeof(line));
        out.text("error", message);
        _radio.send(out.finish());
    }
};

// Try this:
//
//  Exercises 1, 3, 4 and 5 are numbered missions. Keep each mission's state
//  in its own method, and use drive() for movement.
//
//   1. Obstacle stop (mission 1). Stop when a valid front reading is below
//      150 mm and either wheel is moving forward. Why allow reversing?
//
//   2. Speed ramp. Move the wheel speeds towards stored targets on a timer.
//      The loop rate varies, so use nowMs(), not a loop counter.
//
//   3. Drive a square (mission 2), with 300 mm sides. Track the current
//      side or turn and its end time; measure the timing on the real robot.
//
//   4. Wall follower (mission 3). Steer away when too close to the wall and
//      towards it when too far. Then scale the correction with the error
//      (proportional control). Compare the two trajectories.
//
//   5. PID (mission 4). Add D to respond to changes in error, then I to
//      correct persistent error. Scale both by elapsed time, limit the
//      integral to prevent windup, and account for integer rounding.
//      Tune one term at a time and measure the effect on oscillation.
//
//   6. Queue commands. A new movement currently replaces the previous one.
//      Add a queue using commands such as:
//
//          {"queue":1,"left":200,"right":200,"duration":1000}
//          {"queue":1,"left":200,"right":-200,"duration":600}
//          {"run":1}
//
//      Decide whether a new queue command cancels or extends a running queue.
//      {"stop":true} must clear it. Give each step its own drive() deadline;
//      report "done" when the queue is empty. Json.h accepts only flat objects,
//      so an array of steps would require a parser change.
//
//   7. With two missions in place, what state or code do they actually share?
