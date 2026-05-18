// MMC5983MA driver implementation.
//
// I2C and SPI are both first-class bus paths: every read/write method
// branches on which constructor was used and the SPI branch is a real
// implementation, not a stub.
//
// Two hardware quirks (caught by hardware validation in the CircuitPython
// port, not by mocked unit tests) are baked in here:
//   1. readMagneticRaw fires a SET pulse before triggering a single-shot
//      measurement. Without it, the chip's residual offset dominates the
//      reading -- observed |B|~=11 uT where the true field was ~46 uT.
//   2. startContinuousMode clears MEAS_M_DONE and blocks until the chip
//      reasserts it. Without that wait, the first read after CMM_EN sees
//      all-zero data registers and reports |B|~=1419 uT.

#include "MMC5983MA.h"

#include <math.h>

namespace reg = mmc5983ma;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MMC5983MA::MMC5983MA(TwoWire& wire, uint8_t address)
    : _wire(&wire),
      _spi(nullptr),
      _i2c_address(address),
      _cs_pin(0),
      _spi_clock_hz(0),
      _ctrl0_shadow(0),
      _ctrl1_shadow(0),
      _ctrl2_shadow(0),
      _ctrl3_shadow(0),
      _bandwidth_code(reg::BW_100_HZ) {
  for (uint8_t i = 0; i < sizeof(_magBuf); ++i) {
    _magBuf[i] = 0;
  }
}

MMC5983MA::MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz)
    : _wire(nullptr),
      _spi(&spi),
      _i2c_address(0),
      _cs_pin(cs_pin),
      _spi_clock_hz(spi_clock_hz),
      _ctrl0_shadow(0),
      _ctrl1_shadow(0),
      _ctrl2_shadow(0),
      _ctrl3_shadow(0),
      _bandwidth_code(reg::BW_100_HZ) {
  for (uint8_t i = 0; i < sizeof(_magBuf); ++i) {
    _magBuf[i] = 0;
  }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool MMC5983MA::begin() {
  if (_spi != nullptr) {
    pinMode(_cs_pin, OUTPUT);
    digitalWrite(_cs_pin, HIGH);
  }

  if (!isConnected()) {
    return false;
  }
  reset();
  return true;
}

bool MMC5983MA::isConnected() {
  uint8_t prod_id = 0;
  if (!readRegister(reg::REG_PROD_ID, &prod_id)) {
    return false;
  }
  return prod_id == reg::PROD_ID;
}

void MMC5983MA::reset() {
  writeRegister(reg::REG_INT_CTRL_1, reg::CTRL1_SW_RST);
  delay(reg::RESET_DELAY_MS);
  _ctrl0_shadow = 0;
  _ctrl1_shadow = 0;
  _ctrl2_shadow = 0;
  _ctrl3_shadow = 0;
  _bandwidth_code = reg::BW_100_HZ;
}

// ---------------------------------------------------------------------------
// Bus abstraction -- I2C / SPI branches
// ---------------------------------------------------------------------------

bool MMC5983MA::readRegister(uint8_t address, uint8_t* value) {
  if (_wire != nullptr) {
    _wire->beginTransmission(_i2c_address);
    _wire->write(address);
    if (_wire->endTransmission(false) != 0) {
      return false;
    }
    if (_wire->requestFrom((uint8_t)_i2c_address, (uint8_t)1) != 1) {
      return false;
    }
    *value = _wire->read();
    return true;
  }

  // SPI: MSB=1 selects read; lower 7 bits are the register address.
  _spi->beginTransaction(SPISettings(_spi_clock_hz, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs_pin, LOW);
  _spi->transfer((uint8_t)(address | reg::SPI_READ));
  *value = _spi->transfer(0x00);
  digitalWrite(_cs_pin, HIGH);
  _spi->endTransaction();
  return true;
}

bool MMC5983MA::readBurst(uint8_t address, uint8_t* buffer, size_t length) {
  if (_wire != nullptr) {
    _wire->beginTransmission(_i2c_address);
    _wire->write(address);
    if (_wire->endTransmission(false) != 0) {
      return false;
    }
    size_t got = _wire->requestFrom((uint8_t)_i2c_address, (uint8_t)length);
    if (got != length) {
      return false;
    }
    for (size_t i = 0; i < length; ++i) {
      buffer[i] = _wire->read();
    }
    return true;
  }

  // SPI: write address with MSB=1, then clock out `length` dummy bytes;
  // the chip auto-increments its internal address pointer for each byte.
  _spi->beginTransaction(SPISettings(_spi_clock_hz, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs_pin, LOW);
  _spi->transfer((uint8_t)(address | reg::SPI_READ));
  for (size_t i = 0; i < length; ++i) {
    buffer[i] = _spi->transfer(0x00);
  }
  digitalWrite(_cs_pin, HIGH);
  _spi->endTransaction();
  return true;
}

bool MMC5983MA::writeRegister(uint8_t address, uint8_t value) {
  if (_wire != nullptr) {
    _wire->beginTransmission(_i2c_address);
    _wire->write(address);
    _wire->write(value);
    return _wire->endTransmission() == 0;
  }

  // SPI: MSB=0 selects write.
  _spi->beginTransaction(SPISettings(_spi_clock_hz, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs_pin, LOW);
  _spi->transfer((uint8_t)(address & 0x7F));
  _spi->transfer(value);
  digitalWrite(_cs_pin, HIGH);
  _spi->endTransaction();
  return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

uint16_t MMC5983MA::measurementTimeoutMs() const {
  switch (_bandwidth_code) {
    case reg::BW_800_HZ: return reg::MEAS_TIMEOUT_MS_BW_800;
    case reg::BW_400_HZ: return reg::MEAS_TIMEOUT_MS_BW_400;
    case reg::BW_200_HZ: return reg::MEAS_TIMEOUT_MS_BW_200;
    default:             return reg::MEAS_TIMEOUT_MS_BW_100;
  }
}

bool MMC5983MA::triggerMagneticAndWait() {
  // Clear stale data-ready flag (write 1 to clear).
  if (!writeRegister(reg::REG_STATUS, reg::STATUS_MEAS_M_DONE)) {
    return false;
  }
  if (!writeRegister(reg::REG_INT_CTRL_0,
                     (uint8_t)(_ctrl0_shadow | reg::CTRL0_TM_M))) {
    return false;
  }

  const uint16_t timeout = measurementTimeoutMs();
  const uint32_t deadline = millis() + timeout;
  while ((int32_t)(millis() - deadline) <= 0) {
    uint8_t status;
    if (!readRegister(reg::REG_STATUS, &status)) {
      return false;
    }
    if (status & reg::STATUS_MEAS_M_DONE) {
      return true;
    }
    delay(reg::MEAS_POLL_INTERVAL_MS);
  }
  return false;
}

bool MMC5983MA::readRawXYZ(uint32_t* x, uint32_t* y, uint32_t* z) {
  if (!readBurst(reg::REG_X_OUT_0, _magBuf, sizeof(_magBuf))) {
    return false;
  }
  const uint8_t* b = _magBuf;
  *x = ((uint32_t)b[0] << reg::XYZ_0_SHIFT) |
       ((uint32_t)b[1] << reg::XYZ_1_SHIFT) |
       (((uint32_t)b[6] >> reg::XYZ_X2_SHIFT) & reg::XYZ_2_MASK);
  *y = ((uint32_t)b[2] << reg::XYZ_0_SHIFT) |
       ((uint32_t)b[3] << reg::XYZ_1_SHIFT) |
       (((uint32_t)b[6] >> reg::XYZ_Y2_SHIFT) & reg::XYZ_2_MASK);
  *z = ((uint32_t)b[4] << reg::XYZ_0_SHIFT) |
       ((uint32_t)b[5] << reg::XYZ_1_SHIFT) |
       (((uint32_t)b[6] >> reg::XYZ_Z2_SHIFT) & reg::XYZ_2_MASK);
  return true;
}

float MMC5983MA::toMicrotesla(uint32_t raw) {
  return ((float)((int32_t)raw - (int32_t)reg::ZERO_FIELD_OFFSET))
         * reg::SENSITIVITY_UT_PER_LSB;
}

// ---------------------------------------------------------------------------
// Magnetic measurement
// ---------------------------------------------------------------------------

bool MMC5983MA::readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z) {
  // Quirk #1: in single-shot mode, fire SET before the measurement so the
  // bridge polarity is defined. Without this, the reading is dominated by
  // the chip's residual offset and indoor field looks like ~11 uT.
  if ((_ctrl2_shadow & reg::CTRL2_CMM_EN) == 0) {
    setCoil();
    if (!triggerMagneticAndWait()) {
      return false;
    }
  }
  return readRawXYZ(x, y, z);
}

bool MMC5983MA::readMagneticUT(float* x, float* y, float* z) {
  uint32_t rx, ry, rz;
  if (!readMagneticRaw(&rx, &ry, &rz)) {
    return false;
  }
  *x = toMicrotesla(rx);
  *y = toMicrotesla(ry);
  *z = toMicrotesla(rz);
  return true;
}

bool MMC5983MA::readMagneticOffsetCancelled(float* x, float* y, float* z) {
  setCoil();
  if (!triggerMagneticAndWait()) {
    return false;
  }
  uint32_t sx, sy, sz;
  if (!readRawXYZ(&sx, &sy, &sz)) {
    return false;
  }

  resetCoil();
  if (!triggerMagneticAndWait()) {
    return false;
  }
  uint32_t rx, ry, rz;
  if (!readRawXYZ(&rx, &ry, &rz)) {
    return false;
  }

  const float scale = reg::SENSITIVITY_UT_PER_LSB / 2.0f;
  *x = ((int32_t)sx - (int32_t)rx) * scale;
  *y = ((int32_t)sy - (int32_t)ry) * scale;
  *z = ((int32_t)sz - (int32_t)rz) * scale;
  return true;
}

// ---------------------------------------------------------------------------
// Temperature
// ---------------------------------------------------------------------------

float MMC5983MA::readTemperatureC() {
  if (!writeRegister(reg::REG_STATUS, reg::STATUS_MEAS_T_DONE)) {
    return NAN;
  }
  if (!writeRegister(reg::REG_INT_CTRL_0,
                     (uint8_t)(_ctrl0_shadow | reg::CTRL0_TM_T))) {
    return NAN;
  }

  const uint32_t deadline = millis() + reg::TEMP_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) <= 0) {
    uint8_t status;
    if (!readRegister(reg::REG_STATUS, &status)) {
      return NAN;
    }
    if (status & reg::STATUS_MEAS_T_DONE) {
      uint8_t raw;
      if (!readRegister(reg::REG_T_OUT, &raw)) {
        return NAN;
      }
      return reg::TEMP_OFFSET_C + (float)raw * reg::TEMP_LSB_C;
    }
    delay(reg::MEAS_POLL_INTERVAL_MS);
  }
  return NAN;
}

// ---------------------------------------------------------------------------
// SET / RESET coil control
// ---------------------------------------------------------------------------

void MMC5983MA::setCoil() {
  writeRegister(reg::REG_INT_CTRL_0,
                (uint8_t)(_ctrl0_shadow | reg::CTRL0_SET));
  delay(reg::SET_RESET_PULSE_DELAY_MS);
}

void MMC5983MA::resetCoil() {
  writeRegister(reg::REG_INT_CTRL_0,
                (uint8_t)(_ctrl0_shadow | reg::CTRL0_RESET));
  delay(reg::SET_RESET_PULSE_DELAY_MS);
}

void MMC5983MA::setAutomaticSetReset(bool enabled) {
  if (enabled) {
    _ctrl0_shadow |= reg::CTRL0_AUTO_SR_EN;
  } else {
    _ctrl0_shadow = (uint8_t)(_ctrl0_shadow & ~reg::CTRL0_AUTO_SR_EN);
  }
  writeRegister(reg::REG_INT_CTRL_0, _ctrl0_shadow);
}

// ---------------------------------------------------------------------------
// Bandwidth and continuous mode
// ---------------------------------------------------------------------------

bool MMC5983MA::setBandwidth(uint16_t hz) {
  uint8_t code;
  switch (hz) {
    case 100: code = reg::BW_100_HZ; break;
    case 200: code = reg::BW_200_HZ; break;
    case 400: code = reg::BW_400_HZ; break;
    case 800: code = reg::BW_800_HZ; break;
    default:  return false;
  }
  _bandwidth_code = code;
  _ctrl1_shadow = (uint8_t)((_ctrl1_shadow & ~reg::CTRL1_BW_MASK) | code);
  return writeRegister(reg::REG_INT_CTRL_1, _ctrl1_shadow);
}

uint16_t MMC5983MA::getBandwidth() const {
  switch (_bandwidth_code) {
    case reg::BW_800_HZ: return 800;
    case reg::BW_400_HZ: return 400;
    case reg::BW_200_HZ: return 200;
    default:             return 100;
  }
}

bool MMC5983MA::startContinuousMode(uint16_t rate_hz, bool automatic_set_reset) {
  uint8_t freq_code;
  switch (rate_hz) {
    case 1:    freq_code = reg::CM_FREQ_1_HZ; break;
    case 10:   freq_code = reg::CM_FREQ_10_HZ; break;
    case 20:   freq_code = reg::CM_FREQ_20_HZ; break;
    case 50:   freq_code = reg::CM_FREQ_50_HZ; break;
    case 100:  freq_code = reg::CM_FREQ_100_HZ; break;
    case 200:  freq_code = reg::CM_FREQ_200_HZ; break;
    case 1000: freq_code = reg::CM_FREQ_1000_HZ; break;
    default:   return false;
  }

  // Promote bandwidth automatically when the rate requires it.
  if (rate_hz == 1000 && _bandwidth_code != reg::BW_800_HZ) {
    if (!setBandwidth(800)) return false;
  } else if (rate_hz == 200 && _bandwidth_code == reg::BW_100_HZ) {
    if (!setBandwidth(200)) return false;
  }

  if (automatic_set_reset) {
    setAutomaticSetReset(true);
  }

  _ctrl2_shadow = (uint8_t)((_ctrl2_shadow & ~reg::CTRL2_CM_FREQ_MASK)
                            | freq_code
                            | reg::CTRL2_CMM_EN);
  if (!writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow)) {
    return false;
  }

  // Quirk #2: block until the first sample is in the data registers. Without
  // this, the first read after CMM_EN hits all-zero registers and reports
  // |B|~=1419 uT (= sqrt(3)*offset*sensitivity).
  if (!writeRegister(reg::REG_STATUS, reg::STATUS_MEAS_M_DONE)) {
    return false;
  }
  const uint16_t startup_timeout = (uint16_t)(measurementTimeoutMs() + 5);
  const uint32_t deadline = millis() + startup_timeout;
  while ((int32_t)(millis() - deadline) <= 0) {
    uint8_t status;
    if (!readRegister(reg::REG_STATUS, &status)) {
      return false;
    }
    if (status & reg::STATUS_MEAS_M_DONE) {
      return true;
    }
    delay(reg::MEAS_POLL_INTERVAL_MS);
  }
  return false;
}

void MMC5983MA::stopContinuousMode() {
  _ctrl2_shadow = (uint8_t)(_ctrl2_shadow
                            & ~(reg::CTRL2_CMM_EN | reg::CTRL2_CM_FREQ_MASK));
  writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

bool MMC5983MA::isContinuousModeEnabled() const {
  return (_ctrl2_shadow & reg::CTRL2_CMM_EN) != 0;
}

bool MMC5983MA::isDataReady() {
  uint8_t status;
  if (!readRegister(reg::REG_STATUS, &status)) {
    return false;
  }
  return (status & reg::STATUS_MEAS_M_DONE) != 0;
}

// ---------------------------------------------------------------------------
// Periodic SET
// ---------------------------------------------------------------------------

bool MMC5983MA::setPeriodicSet(uint16_t sample_count) {
  uint8_t code;
  switch (sample_count) {
    case 1:    code = reg::PRD_SET_1; break;
    case 25:   code = reg::PRD_SET_25; break;
    case 75:   code = reg::PRD_SET_75; break;
    case 100:  code = reg::PRD_SET_100; break;
    case 250:  code = reg::PRD_SET_250; break;
    case 500:  code = reg::PRD_SET_500; break;
    case 1000: code = reg::PRD_SET_1000; break;
    case 2000: code = reg::PRD_SET_2000; break;
    default:   return false;
  }
  _ctrl2_shadow = (uint8_t)((_ctrl2_shadow & ~reg::CTRL2_PRD_SET_MASK)
                            | (code << reg::CTRL2_PRD_SET_SHIFT)
                            | reg::CTRL2_EN_PRD_SET);
  return writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

void MMC5983MA::disablePeriodicSet() {
  _ctrl2_shadow = (uint8_t)(_ctrl2_shadow
                            & ~(reg::CTRL2_EN_PRD_SET | reg::CTRL2_PRD_SET_MASK));
  writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

// ---------------------------------------------------------------------------
// Interrupts and SPI mode
// ---------------------------------------------------------------------------

void MMC5983MA::setInterruptEnabled(bool enabled) {
  if (enabled) {
    _ctrl0_shadow |= reg::CTRL0_INT_MEAS_DONE_EN;
  } else {
    _ctrl0_shadow = (uint8_t)(_ctrl0_shadow & ~reg::CTRL0_INT_MEAS_DONE_EN);
  }
  writeRegister(reg::REG_INT_CTRL_0, _ctrl0_shadow);
}

void MMC5983MA::clearInterrupt(uint8_t mask) {
  writeRegister(reg::REG_STATUS, mask);
}

void MMC5983MA::setSpi3Wire(bool enabled) {
  if (enabled) {
    _ctrl3_shadow |= reg::CTRL3_SPI_3W;
  } else {
    _ctrl3_shadow = (uint8_t)(_ctrl3_shadow & ~reg::CTRL3_SPI_3W);
  }
  writeRegister(reg::REG_INT_CTRL_3, _ctrl3_shadow);
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

bool MMC5983MA::runSelftest() {
  // Establish a known SET-conditioned baseline. Discarded (used only to
  // place the chip in a known state) before the ST_ENP/ST_ENM swing.
  setCoil();
  if (!triggerMagneticAndWait()) return false;
  uint32_t bx, by, bz;
  if (!readRawXYZ(&bx, &by, &bz)) return false;

  bool ok = false;
  uint32_t px = 0, py = 0, pz = 0;
  uint32_t nx = 0, ny = 0, nz = 0;

  // ST_ENP: positive selftest current.
  _ctrl3_shadow |= reg::CTRL3_ST_ENP;
  if (writeRegister(reg::REG_INT_CTRL_3, _ctrl3_shadow)
      && triggerMagneticAndWait()
      && readRawXYZ(&px, &py, &pz)) {
    // ST_ENM: negative selftest current.
    _ctrl3_shadow = (uint8_t)((_ctrl3_shadow & ~reg::CTRL3_ST_ENP) | reg::CTRL3_ST_ENM);
    if (writeRegister(reg::REG_INT_CTRL_3, _ctrl3_shadow)
        && triggerMagneticAndWait()
        && readRawXYZ(&nx, &ny, &nz)) {
      ok = true;
    }
  }

  // Always end with a clean SET so the device is ready for normal use.
  _ctrl3_shadow = (uint8_t)(_ctrl3_shadow & ~(reg::CTRL3_ST_ENP | reg::CTRL3_ST_ENM));
  writeRegister(reg::REG_INT_CTRL_3, _ctrl3_shadow);
  setCoil();

  if (!ok) return false;

  const float dx = fabsf(toMicrotesla(px) - toMicrotesla(nx));
  const float dy = fabsf(toMicrotesla(py) - toMicrotesla(ny));
  const float dz = fabsf(toMicrotesla(pz) - toMicrotesla(nz));
  return dx > reg::SELFTEST_THRESHOLD_UT
      && dy > reg::SELFTEST_THRESHOLD_UT
      && dz > reg::SELFTEST_THRESHOLD_UT;
}
