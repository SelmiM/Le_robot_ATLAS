#pragma once
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "Config.h"

/**
 * Grove 16x2 LCD on I2C1, using the AIP31068L text controller at 0x3E.
 *
 *     display.line(0, "L 200  R 200");
 *     display.line(1, "front 412mm");
 *
 * A full two-line write takes about 3 ms at 100 kHz and blocks the loop.
 * Robot.h limits refreshes and skips unchanged rows; every I2C transfer has
 * a timeout so a disconnected display cannot stall the safety check.
 * The Grove RGB LCD supports the same text controller but its backlight is
 * not controlled here. PCF8574 backpacks use a different protocol.
 */
class Display {
public:
  static const int COLUMNS = 16;
  static const int ROWS    = 2;

  // 100 kHz, the speed every I2C device is required to manage. The chip will
  // do 400 kHz and TRY THIS 3 is about what that buys you.
  static const uint BUS_HZ = 100 * 1000;

  // Long enough that a transfer which is going to work has worked, short
  // enough that one that is not going to work costs nothing worth measuring.
  // A whole line is seventeen bytes, about 1.5 ms at 100 kHz, so LINE_ gets
  // more room than a two-byte command does.
  static const uint TIMEOUT_US      = 2000;
  static const uint LINE_TIMEOUT_US = 5000;

  // Returns false if the display is switched off in Config.h, or did not
  // answer. Safe to call either way, and safe to call everything else after
  // it either way: every method below does nothing when _ready is false.
  bool begin() {
    if (!DISPLAY_ENABLED) return false;

    i2c_init(i2c1, BUS_HZ);
    gpio_set_function(DISPLAY_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_SCL_PIN, GPIO_FUNC_I2C);
    // I2C lines are pulled up, never driven high: any device may hold one low
    // at any time, which is how a bus with no chip select works out whose turn
    // it is. The Grove module has its own pull-ups, so these are belt and
    // braces, and harmless where the module already provides them.
    gpio_pull_up(DISPLAY_SDA_PIN);
    gpio_pull_up(DISPLAY_SCL_PIN);

    // The datasheet asks for 50 ms after power before the first command.
    sleep_ms(50);

    // Function set, three times. This is not superstition and not a retry:
    // the controller powers up in an eight-bit-parallel state machine that
    // only leaves it after it has seen this pattern, and the delays between
    // are minimums from the datasheet. Every HD44780-compatible chip since
    // 1985 wants this, which is why the sequence looks so odd.
    //
    //   0x38 = function set, 8-bit interface, 2 lines, 5x8 dot characters.
    if (!command(0x38)) return false;   // if the first one is not acknowledged,
    sleep_ms(5);                        // there is nothing on the bus
    command(0x38);
    sleep_us(150);
    command(0x38);

    command(0x0C);       // display on, cursor off, no blinking
    command(0x01);       // clear
    sleep_ms(2);         // ...which is the one command that takes real time
    command(0x06);       // entry mode: left to right, do not scroll the screen

    _ready = true;
    for (int row = 0; row < ROWS; row++) _shown[row][0] = '\0';
    return true;
  }

  bool connected() const { return _ready; }

  // Writes one row, padded with spaces to the full 16 columns. Padding rather
  // than clearing is deliberate: clear (0x01) is the slowest command the chip
  // has, and a screen cleared before every write flickers visibly.
  //
  // Anything past 16 characters is dropped. A display is not a log.
  void line(int row, const char* text) {
    if (!_ready || row < 0 || row >= ROWS) return;

    // Row 0 starts at address 0x00 and row 1 at 0x40. They are NOT next to
    // each other in the chip's memory, and on a 16-column module the 24
    // addresses between them are real but not attached to any pixels.
    if (!command(0x80 | (row == 0 ? 0x00 : 0x40))) return;

    // One transfer: a 0x40 control byte says "everything after this is
    // characters", so all sixteen go out back to back rather than as sixteen
    // separate two-byte transfers, which would take four times as long.
    uint8_t message[1 + COLUMNS];
    message[0] = 0x40;

    int i = 0;
    while (i < COLUMNS && text[i] != '\0') { message[1 + i] = (uint8_t)text[i]; i++; }
    while (i < COLUMNS)                     { message[1 + i] = (uint8_t)' ';    i++; }

    if (i2c_write_timeout_us(i2c1, DISPLAY_ADDRESS, message, sizeof(message),
                             false, LINE_TIMEOUT_US) < 0) {
      // Nothing to do about it and nothing worth stopping for: the display is
      // the one device on this robot whose failure changes nothing else.
      return;
    }

    // Remember what is up there, so Robot.h can skip a write that would not
    // change anything. See point 2 in the header comment.
    strncpy(_shown[row], text, COLUMNS);
    _shown[row][COLUMNS] = '\0';
  }

  // True if `text` is already on that row, so the caller can skip the write.
  bool alreadyShowing(int row, const char* text) const {
    if (row < 0 || row >= ROWS) return true;
    return strncmp(_shown[row], text, COLUMNS) == 0;
  }

private:
  bool _ready = false;
  char _shown[ROWS][COLUMNS + 1] = {};   // what we last put on each row

  // One command byte, prefixed by a 0x00 control byte meaning "this is an
  // instruction, not a character". Characters use 0x40 instead; that single
  // bit is the whole difference between "clear the screen" and printing a
  // character whose code happens to be 1.
  bool command(uint8_t value) {
    const uint8_t message[2] = { 0x00, value };
    return i2c_write_timeout_us(i2c1, DISPLAY_ADDRESS, message, 2,
                                false, TIMEOUT_US) == 2;
  }
};

// Try this:
//
//   1. Add scroll(int row, const char* text) for a message longer than 16
//      characters, moving one character left every 300 ms. Remember rule 1
//      above: on a timer, and never with sleep_ms().
//   2. The chip has eight slots for characters you draw yourself, eight bytes
//      each, five bits per row. Add customChar(int slot, const uint8_t* rows)
//      and put a battery symbol on the screen. Command 0x40 | (slot * 8) sets
//      where the next data bytes go.
//   3. Measure claim 2 above. Put a GPIO high in line(), low at the end of it,
//      and look at the pin. How long IS a refresh, and what happens to it if
//      you take BUS_HZ to 400 kHz?
//   4. Robot.h decides what goes on the screen. Change it to show the front
//      distance as a bar of 16 blocks rather than a number, and decide which
//      of the two you would rather have while chasing a robot across a room.
//   5. Make {"display": "hello"} work, so your own program can put text up
//      there. This is not a Display.h exercise: Json.h stores every value
//      as a `long` and has nowhere to put a string, so the parser is what you
//      have to change first. Where does the text live once it is parsed, given
//      that nothing on this robot allocates memory while it is running?
