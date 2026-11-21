// See VL53L1X.h. You do not need to read this file.
//
// The register addresses, the default configuration block, the per-mode
// timing values and the range status table below are taken from ST's VL53L1X
// Ultra Lite Driver (STSW-IMG009), the reference driver ST publishes for this
// part, under its BSD-3-Clause licence:
//
// Copyright (c) 2017, STMicroelectronics - All Rights Reserved
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "VL53L1X.h"

// All register addresses are 16-bit.
static const uint16_t VHV_CONFIG__TIMEOUT_MACROP_BOUND  = 0x0008;
static const uint16_t GPIO_HV_MUX__CTRL                 = 0x0030;
static const uint16_t GPIO__TIO_HV_STATUS               = 0x0031;
static const uint16_t PHASECAL_CONFIG__TIMEOUT_MACROP   = 0x004B;
static const uint16_t RANGE_CONFIG__TIMEOUT_MACROP_A_HI = 0x005E;
static const uint16_t RANGE_CONFIG__VCSEL_PERIOD_A      = 0x0060;
static const uint16_t RANGE_CONFIG__TIMEOUT_MACROP_B_HI = 0x0061;
static const uint16_t RANGE_CONFIG__VCSEL_PERIOD_B      = 0x0063;
static const uint16_t RANGE_CONFIG__VALID_PHASE_HIGH    = 0x0069;
static const uint16_t SYSTEM__INTERMEASUREMENT_PERIOD   = 0x006C;
static const uint16_t SD_CONFIG__WOI_SD0                = 0x0078;
static const uint16_t SD_CONFIG__INITIAL_PHASE_SD0      = 0x007A;
static const uint16_t SYSTEM__INTERRUPT_CLEAR           = 0x0086;
static const uint16_t SYSTEM__MODE_START                = 0x0087;
static const uint16_t RESULT__RANGE_STATUS              = 0x0089;
static const uint16_t RESULT__RANGE_MM                  = 0x0096;
static const uint16_t RESULT__SIGNAL_RATE               = 0x0098;
static const uint16_t RESULT__OSC_CALIBRATE_VAL         = 0x00DE;
static const uint16_t FIRMWARE__SYSTEM_STATUS           = 0x00E5;
static const uint16_t IDENTIFICATION__MODEL_ID          = 0x010F;

static const uint16_t EXPECTED_MODEL_ID = 0xEACC;

// ST's default configuration: 91 bytes for registers 0x2D..0x87, in order.
// Array index determines the register address; do not reorder it.
static const uint8_t kDefaultConfiguration[] = {
  0x00, /* 0x2d */ 0x00, /* 0x2e */ 0x00, /* 0x2f */ 0x01, /* 0x30 */
  0x02, /* 0x31 */ 0x00, /* 0x32 */ 0x02, /* 0x33 */ 0x08, /* 0x34 */
  0x00, /* 0x35 */ 0x08, /* 0x36 */ 0x10, /* 0x37 */ 0x01, /* 0x38 */
  0x01, /* 0x39 */ 0x00, /* 0x3a */ 0x00, /* 0x3b */ 0x00, /* 0x3c */
  0x00, /* 0x3d */ 0xff, /* 0x3e */ 0x00, /* 0x3f */ 0x0F, /* 0x40 */
  0x00, /* 0x41 */ 0x00, /* 0x42 */ 0x00, /* 0x43 */ 0x00, /* 0x44 */
  0x00, /* 0x45 */ 0x20, /* 0x46 : interrupt on new sample ready */
  0x0b, /* 0x47 */ 0x00, /* 0x48 */ 0x00, /* 0x49 */ 0x02, /* 0x4a */
  0x0a, /* 0x4b */ 0x21, /* 0x4c */ 0x00, /* 0x4d */ 0x00, /* 0x4e */
  0x05, /* 0x4f */ 0x00, /* 0x50 */ 0x00, /* 0x51 */ 0x00, /* 0x52 */
  0x00, /* 0x53 */ 0xc8, /* 0x54 */ 0x00, /* 0x55 */ 0x00, /* 0x56 */
  0x38, /* 0x57 */ 0xff, /* 0x58 */ 0x01, /* 0x59 */ 0x00, /* 0x5a */
  0x08, /* 0x5b */ 0x00, /* 0x5c */ 0x00, /* 0x5d */ 0x01, /* 0x5e */
  0xcc, /* 0x5f */ 0x0f, /* 0x60 */ 0x01, /* 0x61 */ 0xf1, /* 0x62 */
  0x0d, /* 0x63 */ 0x01, /* 0x64 : sigma threshold, high byte */
  0x68, /* 0x65 : sigma threshold, low byte */
  0x00, /* 0x66 : minimum count rate, high byte */
  0x80, /* 0x67 : minimum count rate, low byte */
  0x08, /* 0x68 */ 0xb8, /* 0x69 */ 0x00, /* 0x6a */ 0x00, /* 0x6b */
  0x00, /* 0x6c : time between measurements, 4 bytes */
  0x00, /* 0x6d */ 0x0f, /* 0x6e */ 0x89, /* 0x6f */
  0x00, /* 0x70 */ 0x00, /* 0x71 */ 0x00, /* 0x72 */ 0x00, /* 0x73 */
  0x00, /* 0x74 */ 0x00, /* 0x75 */ 0x00, /* 0x76 */ 0x01, /* 0x77 */
  0x0f, /* 0x78 */ 0x0d, /* 0x79 */ 0x0e, /* 0x7a */ 0x0e, /* 0x7b */
  0x00, /* 0x7c */ 0x00, /* 0x7d */ 0x02, /* 0x7e */
  0xc7, /* 0x7f : centre of the region of interest */
  0xff, /* 0x80 : width and height of the region of interest */
  0x9B, /* 0x81 */ 0x00, /* 0x82 */ 0x00, /* 0x83 */ 0x00, /* 0x84 */
  0x01, /* 0x85 */ 0x00, /* 0x86 */ 0x00, /* 0x87 */
};

static const uint16_t CONFIG_FIRST_REGISTER = 0x2D;

// Each I2C transfer starts with the 16-bit register address, high byte first.

void VL53L1X::writeByte(uint16_t reg, uint8_t value) {
  const uint8_t packet[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), value };
  if (i2c_write_timeout_us(_bus, ADDRESS, packet, 3, false, TIMEOUT_US) != 3) _lost = true;
}

void VL53L1X::writeWord(uint16_t reg, uint16_t value) {
  const uint8_t packet[4] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
                              (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  if (i2c_write_timeout_us(_bus, ADDRESS, packet, 4, false, TIMEOUT_US) != 4) _lost = true;
}

void VL53L1X::writeDword(uint16_t reg, uint32_t value) {
  const uint8_t packet[6] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
                              (uint8_t)(value >> 24), (uint8_t)(value >> 16),
                              (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  if (i2c_write_timeout_us(_bus, ADDRESS, packet, 6, false, TIMEOUT_US) != 6) _lost = true;
}

uint8_t VL53L1X::readByte(uint16_t reg) {
  const uint8_t address[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
  uint8_t value = 0;
  // `true` keeps control of the bus between writing the address and reading
  // the answer, so nothing else can interrupt the pair.
  if (i2c_write_timeout_us(_bus, ADDRESS, address, 2, true, TIMEOUT_US) != 2 ||
      i2c_read_timeout_us(_bus, ADDRESS, &value, 1, false, TIMEOUT_US) != 1) {
    _lost = true;
    return 0;
  }
  return value;
}

uint16_t VL53L1X::readWord(uint16_t reg) {
  const uint8_t address[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
  uint8_t answer[2];
  if (i2c_write_timeout_us(_bus, ADDRESS, address, 2, true, TIMEOUT_US) != 2 ||
      i2c_read_timeout_us(_bus, ADDRESS, answer, 2, false, TIMEOUT_US) != 2) {
    _lost = true;
    return 0;
  }
  return (uint16_t)((answer[0] << 8) | answer[1]);
}

bool VL53L1X::begin(i2c_inst_t* bus) {
  _bus  = bus;
  _lost = false;

  // Is anything there, and is it the chip we think it is?
  if (readWord(IDENTIFICATION__MODEL_ID) != EXPECTED_MODEL_ID || _lost) return false;

  // The sensor runs its own firmware and needs a moment after power-up.
  // Bit 0 of this register goes to 1 when it has finished booting.
  absolute_time_t giveUp = make_timeout_time_ms(200);
  while (!(readByte(FIRMWARE__SYSTEM_STATUS) & 0x01)) {
    if (_lost || time_reached(giveUp)) return false;
    sleep_ms(2);
  }

  // Write the whole configuration block, one register at a time.
  for (size_t i = 0; i < sizeof(kDefaultConfiguration); i++)
    writeByte(CONFIG_FIRST_REGISTER + i, kDefaultConfiguration[i]);
  if (_lost) return false;

  // ST's driver takes one throwaway measurement here: it makes the sensor
  // work out its own calibration, which the next two writes then lock in.
  startRanging();
  if (!waitForData(200)) return false;
  clearInterrupt();
  stopRanging();

  writeByte(VHV_CONFIG__TIMEOUT_MACROP_BOUND, 0x09);
  writeByte(0x000B, 0x00);

  return !_lost;
}

bool VL53L1X::waitForData(uint32_t timeoutMs) {
  absolute_time_t giveUp = make_timeout_time_ms(timeoutMs);
  while (!dataReady()) {
    if (_lost || time_reached(giveUp)) return false;
    sleep_ms(1);
  }
  return true;
}

void VL53L1X::setMode(Mode mode) {
  // These six values per mode change the laser pulse length and the window
  // the sensor looks in. Straight from ST's driver; there is no derivation
  // to show, because ST does not publish one.
  if (mode == Mode::Short) {
    writeByte(PHASECAL_CONFIG__TIMEOUT_MACROP,  0x14);
    writeByte(RANGE_CONFIG__VCSEL_PERIOD_A,     0x07);
    writeByte(RANGE_CONFIG__VCSEL_PERIOD_B,     0x05);
    writeByte(RANGE_CONFIG__VALID_PHASE_HIGH,   0x38);
    writeWord(SD_CONFIG__WOI_SD0,               0x0705);
    writeWord(SD_CONFIG__INITIAL_PHASE_SD0,     0x0606);
    // Matching timing budget: about 33 ms per measurement.
    writeWord(RANGE_CONFIG__TIMEOUT_MACROP_A_HI, 0x00D6);
    writeWord(RANGE_CONFIG__TIMEOUT_MACROP_B_HI, 0x006E);
  } else {
    writeByte(PHASECAL_CONFIG__TIMEOUT_MACROP,  0x0A);
    writeByte(RANGE_CONFIG__VCSEL_PERIOD_A,     0x0F);
    writeByte(RANGE_CONFIG__VCSEL_PERIOD_B,     0x0D);
    writeByte(RANGE_CONFIG__VALID_PHASE_HIGH,   0xB8);
    writeWord(SD_CONFIG__WOI_SD0,               0x0F0D);
    writeWord(SD_CONFIG__INITIAL_PHASE_SD0,     0x0E0E);
    // Long mode needs different timing words for the same 33 ms budget.
    writeWord(RANGE_CONFIG__TIMEOUT_MACROP_A_HI, 0x0060);
    writeWord(RANGE_CONFIG__TIMEOUT_MACROP_B_HI, 0x006E);
  }
}

void VL53L1X::setIntervalMs(uint32_t ms) {
  // The period is counted in ticks of the chip's own oscillator, whose rate
  // it measured at the factory. 1.075 is ST's correction factor, written
  // here as 1075 / 1000 to stay in integers.
  uint32_t clockPll = readWord(RESULT__OSC_CALIBRATE_VAL) & 0x3FF;
  writeDword(SYSTEM__INTERMEASUREMENT_PERIOD, clockPll * ms * 1075 / 1000);
}

void VL53L1X::startRanging() { writeByte(SYSTEM__MODE_START, 0x40); }
void VL53L1X::stopRanging()  { writeByte(SYSTEM__MODE_START, 0x00); }

bool VL53L1X::dataReady() {
  // The sensor signals "ready" by driving a pin, and which level counts as
  // ready is itself configurable -- so read the polarity, then the pin.
  uint8_t polarity = ((readByte(GPIO_HV_MUX__CTRL) >> 4) & 0x01) ? 0 : 1;
  uint8_t status   = readByte(GPIO__TIO_HV_STATUS);
  return !_lost && (status & 0x01) == polarity;
}

uint16_t VL53L1X::readDistanceMm() { return readWord(RESULT__RANGE_MM); }
uint16_t VL53L1X::readSignalRate() { return readWord(RESULT__SIGNAL_RATE); }

uint8_t VL53L1X::readRangeStatus() {
  uint8_t raw = readByte(RESULT__RANGE_STATUS) & 0x1F;
  if (_lost) return 255;

  // The chip's internal codes are not in a helpful order, so ST's driver
  // translates them through this table. We keep the translation so 0 means
  // "good", as every other VL53L1X library reports it:
  //   0 good                  1 sigma too high: the reading is too uncertain
  //   2 signal too weak       3 below the minimum range
  //   4 out of bounds         5 hardware fault
  //   6 no new measurement    7 wrapped around: something far looked near
  //   9 crosstalk check      10/11/12/13 other internal checks
  static const uint8_t kStatus[24] = {
    255, 255, 255,   5,   2,   4,   1,   7,   3,   0,
    255, 255,   9,  13, 255, 255, 255, 255,  10,   6,
    255, 255,  11,  12,
  };
  return raw < sizeof(kStatus) ? kStatus[raw] : 255;
}

void VL53L1X::clearInterrupt() { writeByte(SYSTEM__INTERRUPT_CLEAR, 0x01); }
