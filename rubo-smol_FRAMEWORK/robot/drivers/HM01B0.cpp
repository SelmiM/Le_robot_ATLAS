// See HM01B0.h. You do not need to read this file.
#include "HM01B0.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// PIO program for reading pixels. Original assembly (ArduCAM image.pio),
// one bit of pixel data per clock:
//      wait 1 pin 9      ; wait for HREF  (d0 + 9)
//      wait 1 pin 8      ; wait for PCLK rising (d0 + 8)
//      in   pins, 1      ; shift in one data bit from d0
//      wait 0 pin 8      ; wait for PCLK falling
//
// Pre-assembled with D0 = 0 to avoid a pioasm build step. WAIT instructions
// use absolute GPIO numbers, so begin() adjusts them for the configured pins.
// There are no jumps, so the program works at any load address.
static const uint16_t kImageInstructions[] = {
  0x20a9,  // wait 1 pin 9
  0x20a8,  // wait 1 pin 8
  0x4001,  // in pins, 1
  0x2028,  // wait 0 pin 8
};

// 324x244 grayscale settings from ArduCAM's hm01b0_init.h.
// {0xFFFF, 0xFF} marks the end of the list.
struct SensorReg { uint16_t reg; uint8_t val; };

static const SensorReg kInitRegisters[] = {
  {0x0103, 0x0}, {0x0100, 0x00}, {0x1003, 0x08}, {0x1007, 0x08},
  {0x3044, 0x0A}, {0x3045, 0x00}, {0x3047, 0x0A}, {0x3050, 0xC0},
  {0x3051, 0x42}, {0x3052, 0x50}, {0x3053, 0x00}, {0x3054, 0x03},
  {0x3055, 0xF7}, {0x3056, 0xF8}, {0x3057, 0x29}, {0x3058, 0x1F},
  {0x3059, 0x1E}, {0x3064, 0x00}, {0x3065, 0x04}, {0x1000, 0x43},
  {0x1001, 0x40}, {0x1002, 0x32}, {0x0350, 0x7F}, {0x1006, 0x01},
  {0x1008, 0x00}, {0x1009, 0xA0}, {0x100A, 0x60}, {0x100B, 0x90},
  {0x100C, 0x40}, {0x3022, 0x01}, {0x1012, 0x01}, {0x2000, 0x07},
  {0x2003, 0x00}, {0x2004, 0x1C}, {0x2007, 0x00}, {0x2008, 0x58},
  {0x200B, 0x00}, {0x200C, 0x7A}, {0x200F, 0x00}, {0x2010, 0xB8},
  {0x2013, 0x00}, {0x2014, 0x58}, {0x2017, 0x00}, {0x2018, 0x9B},
  {0x2100, 0x01}, {0x2101, 0x5F}, {0x2102, 0x0A}, {0x2103, 0x03},
  {0x2104, 0x05}, {0x2105, 0x02}, {0x2106, 0x14}, {0x2107, 0x02},
  {0x2108, 0x03}, {0x2109, 0x03}, {0x210A, 0x00}, {0x210B, 0x80},
  {0x210C, 0x40}, {0x210D, 0x20}, {0x210E, 0x03}, {0x210F, 0x00},
  {0x2110, 0x85}, {0x2111, 0x00}, {0x2112, 0xA0}, {0x2150, 0x03},
  {0x0340, 0x01}, {0x0341, 0x7A}, {0x0342, 0x01}, {0x0343, 0x77},
  {0x3010, 0x00}, {0x0383, 0x01}, {0x0387, 0x01}, {0x0390, 0x00},
  {0x3011, 0x70}, {0x3059, 0x22}, {0x3060, 0x30}, {0x0101, 0x01},
  {0x0104, 0x01}, {0x0100, 0x01}, {0xFFFF, 0xFF},
};

// Bit-banged SCCB on GPIO: camera registers use 16-bit addresses and
// non-standard timing.

static const uint8_t  kSensorAddress = 0x24;
static const unsigned kHalfBitUs     = 10;   // ~50 kHz

#define SCL_HIGH() gpio_put(_pins.sioc, 1)
#define SCL_LOW()  gpio_put(_pins.sioc, 0)
#define SDA_HIGH() gpio_put(_pins.siod, 1)
#define SDA_LOW()  gpio_put(_pins.siod, 0)

void HM01B0::writeRegister(uint16_t reg, uint8_t value) {
  const uint8_t bytes[3] = {
    (uint8_t)(reg >> 8),
    (uint8_t)(reg & 0xFF),
    value,
  };

  // start condition
  SDA_HIGH(); sleep_us(kHalfBitUs);
  SCL_HIGH(); sleep_us(kHalfBitUs);
  SDA_LOW();  sleep_us(kHalfBitUs);
  SCL_LOW();  sleep_us(kHalfBitUs);

  // address byte (write), then the three payload bytes
  uint8_t frame[4] = { (uint8_t)(kSensorAddress << 1), bytes[0], bytes[1], bytes[2] };
  for (int b = 0; b < 4; b++) {
    for (int i = 0; i < 8; i++) {
      if ((frame[b] << i) & 0x80) SDA_HIGH(); else SDA_LOW();
      sleep_us(kHalfBitUs);
      SCL_HIGH(); sleep_us(kHalfBitUs);
      SCL_LOW();
    }
    // read the acknowledge bit (we do not act on it, the camera is write-only here)
    gpio_set_dir(_pins.siod, GPIO_IN);
    sleep_us(kHalfBitUs);
    SCL_HIGH(); sleep_us(kHalfBitUs);
    SCL_LOW();
    gpio_set_dir(_pins.siod, GPIO_OUT);
    sleep_us(kHalfBitUs);
  }

  // stop condition
  SDA_LOW();  sleep_us(kHalfBitUs);
  SCL_HIGH(); sleep_us(kHalfBitUs);
  SDA_HIGH(); sleep_us(kHalfBitUs);
}

bool HM01B0::begin(const HM01B0Pins& pins) {
  // Catch a mistyped pin number in Config.h before it becomes a mystery.
  // The Pico has GP0..GP29, and the pixel reader needs d0+9 to still be a pin.
  const uint highest = 29;
  if (pins.resetb > highest || pins.xclk > highest || pins.siod > highest ||
      pins.sioc   > highest || pins.vsync > highest || pins.d0 + 9 > highest) {
    return false;
  }

  _pins = pins;

  // The template was assembled for D0 = 0, where PCLK is 8 and HREF is 9. WAIT
  // pin numbers are ABSOLUTE GPIO indices -- there is no base offset for them
  // (sm_config_set_in_pins only moves the data read) -- so re-point the three
  // waits at this wiring's pins before the program is loaded.
  _programWords[0] = (kImageInstructions[0] & ~0x1Fu) | ((pins.d0 + 9) & 0x1Fu);   // wait 1 HREF
  _programWords[1] = (kImageInstructions[1] & ~0x1Fu) | ((pins.d0 + 8) & 0x1Fu);   // wait 1 PCLK rising
  _programWords[2] = kImageInstructions[2];                                        // in pins, 1
  _programWords[3] = (kImageInstructions[3] & ~0x1Fu) | ((pins.d0 + 8) & 0x1Fu);   // wait 0 PCLK falling

  // 1. Generate the camera's master clock (~20 MHz) on the XCLK pin.
  //    Uses the checked local rather than _pins.xclk so the compiler can see
  //    for itself that the PWM slice number is in range.
  const uint xclk = pins.xclk;
  gpio_set_function(xclk, GPIO_FUNC_PWM);
  // The & 7 is a no-op for a real GPIO (the RP2040 has exactly 8 PWM slices);
  // it is there so the compiler can prove the slice index is in range.
  uint slice = pwm_gpio_to_slice_num(xclk) & 7u;
  pwm_set_wrap(slice, 9);          // 125 MHz / (9 + 1) = 12.5 MHz
  pwm_set_gpio_level(xclk, 3);
  pwm_set_enabled(slice, true);

  // 2. Prepare the bit-banged I2C pins.
  gpio_init(_pins.sioc); gpio_set_dir(_pins.sioc, GPIO_OUT);
  gpio_init(_pins.siod); gpio_set_dir(_pins.siod, GPIO_OUT);

  // 3. Reset the camera and let it wake up.
  gpio_init(_pins.resetb); gpio_set_dir(_pins.resetb, GPIO_OUT);
  gpio_put(_pins.resetb, 0); sleep_ms(100);
  gpio_put(_pins.resetb, 1); sleep_ms(100);

  // 4. Push the register table.
  for (const SensorReg* r = kInitRegisters; r->reg != 0xFFFF; r++) {
    writeRegister(r->reg, r->val);
  }

  // 5. Claim a PIO state machine. On a Pico W the WiFi chip already uses one,
  //    so we ask for a free one rather than hard-coding pio0 / sm0.
  PIO candidates[] = { pio0, pio1 };
  for (PIO pio : candidates) {
    pio_program_t program = {};
    program.instructions = _programWords;
    program.length       = 4;
    program.origin       = -1;   // let the SDK place it anywhere

    int sm = pio_claim_unused_sm(pio, false);       // false: do not panic on failure
    if (sm < 0) continue;
    if (!pio_can_add_program(pio, &program)) {
      pio_sm_unclaim(pio, sm);
      continue;
    }

    uint offset = pio_add_program(pio, &program);
    pio_sm_set_consecutive_pindirs(pio, sm, _pins.d0, 1, false);   // d0 is an input

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 3);
    sm_config_set_in_pins(&c, _pins.d0);
    sm_config_set_in_shift(&c, false, true, 8);   // MSB first, autopush every 8 bits
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, sm, offset, &c);

    _pio = pio;
    _sm  = (uint)sm;
    break;
  }
  if (_pio == nullptr) return false;

  // 6. Claim a DMA channel to move pixels without the CPU.
  _dmaChannel = dma_claim_unused_channel(false);
  if (_dmaChannel < 0) return false;

  _ready = true;
  return true;
}

// Returns false if the camera did not deliver a frame in time. Every wait in
// here is bounded: a camera that is not clocking must never be able to freeze
// the robot, because the motors could still be running.
bool HM01B0::captureFrame(uint8_t* buffer, size_t bufferSize) {
  if (!_ready || buffer == nullptr) return false;

  const size_t frameSize = (size_t)HM01B0_WIDTH * HM01B0_HEIGHT;
  if (bufferSize > frameSize) bufferSize = frameSize;

  PIO pio = (PIO)_pio;

  dma_channel_config c = dma_channel_get_default_config(_dmaChannel);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, true);
  channel_config_set_dreq(&c, pio_get_dreq(pio, _sm, false));
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);

  dma_channel_configure(_dmaChannel, &c,
                        buffer,              // write to
                        &pio->rxf[_sm],      // read from
                        bufferSize,
                        false);              // do not start yet

  // Line up with the start of a frame: wait for VSYNC to go low, then high.
  if (!waitForVsync(false) || !waitForVsync(true)) {
    resetStateMachine(pio);
    return false;
  }

  dma_channel_start(_dmaChannel);
  pio_sm_set_enabled(pio, _sm, true);

  bool completed = true;
  uint32_t startedAt = to_ms_since_boot(get_absolute_time());
  while (dma_channel_is_busy(_dmaChannel)) {
    if (to_ms_since_boot(get_absolute_time()) - startedAt > CAPTURE_TIMEOUT_MS) {
      dma_channel_abort(_dmaChannel);
      completed = false;
      break;
    }
  }

  resetStateMachine(pio);
  return completed;
}

// Waits for VSYNC to reach the given level. False if it never does.
bool HM01B0::waitForVsync(bool level) {
  uint32_t startedAt = to_ms_since_boot(get_absolute_time());
  while (gpio_get(_pins.vsync) != level) {
    if (to_ms_since_boot(get_absolute_time()) - startedAt > CAPTURE_TIMEOUT_MS) return false;
    tight_loop_contents();
  }
  return true;
}

// Stops the state machine and drops any half-read pixels, so the next capture
// starts from a known-clean state even if this one went wrong.
void HM01B0::resetStateMachine(void* pioInstance) {
  PIO pio = (PIO)pioInstance;
  pio_sm_set_enabled(pio, _sm, false);
  pio_sm_clear_fifos(pio, _sm);
  pio_sm_restart(pio, _sm);
}
