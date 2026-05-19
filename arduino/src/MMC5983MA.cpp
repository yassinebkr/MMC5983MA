// MMC5983MA driver implementation.
//
// The three bus-abstraction methods (readRegister, readBurst, writeRegister)
// live inline at the bottom of MMC5983MA.h so the compiler can inline them
// into the rest of the driver and into the user's sketch.
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
//
// Every public method that talks to the bus clears _last_error on entry
// and sets it on the failure path so lastError() always reflects the
// most recent call's outcome.

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
      _spi_settings(SPISettings(8000000UL, MSBFIRST, SPI_MODE0)),
      _ctrl0_shadow(0),
      _ctrl1_shadow(0),
      _ctrl2_shadow(0),
      _ctrl3_shadow(0),
      _bandwidth_code(reg::BW_100_HZ),
      _last_error(Error::None) {
  for (uint8_t i = 0; i < sizeof(_magBuf); ++i) {
    _magBuf[i] = 0;
  }
}

MMC5983MA::MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz)
    : _wire(nullptr),
      _spi(&spi),
      _i2c_address(0),
      _cs_pin(cs_pin),
      _spi_settings(SPISettings(spi_clock_hz, MSBFIRST, SPI_MODE0)),
      _ctrl0_shadow(0),
      _ctrl1_shadow(0),
      _ctrl2_shadow(0),
      _ctrl3_shadow(0),
      _bandwidth_code(reg::BW_100_HZ),
      _last_error(Error::None) {
  for (uint8_t i = 0; i < sizeof(_magBuf); ++i) {
    _magBuf[i] = 0;
  }
}

MMC5983MA::MMC5983MA(SPIClass& spi, uint8_t cs_pin, SPISettings settings)
    : _wire(nullptr),
      _spi(&spi),
      _i2c_address(0),
      _cs_pin(cs_pin),
      _spi_settings(settings),
      _ctrl0_shadow(0),
      _ctrl1_shadow(0),
      _ctrl2_shadow(0),
      _ctrl3_shadow(0),
      _bandwidth_code(reg::BW_100_HZ),
      _last_error(Error::None) {
  for (uint8_t i = 0; i < sizeof(_magBuf); ++i) {
    _magBuf[i] = 0;
  }
}

void MMC5983MA::setSpiSettings(SPISettings settings) {
  _spi_settings = settings;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool MMC5983MA::begin() {
  _last_error = Error::None;
  if (_spi != nullptr) {
    pinMode(_cs_pin, OUTPUT);
    digitalWrite(_cs_pin, HIGH);
  }

  uint8_t prod_id = 0;
  if (!readRegister(reg::REG_PROD_ID, &prod_id)) {
    return false;
  }
  if (prod_id != reg::PROD_ID) {
    _last_error = Error::IdMismatch;
    return false;
  }
  reset();
  return true;
}

bool MMC5983MA::isConnected() {
  _last_error = Error::None;
  uint8_t prod_id = 0;
  if (!readRegister(reg::REG_PROD_ID, &prod_id)) {
    return false;
  }
  if (prod_id != reg::PROD_ID) {
    _last_error = Error::IdMismatch;
    return false;
  }
  return true;
}

void MMC5983MA::reset() {
  _last_error = Error::None;
  writeRegister(reg::REG_INT_CTRL_1, reg::CTRL1_SW_RST);
  delay(reg::RESET_DELAY_MS);
  _ctrl0_shadow = 0;
  _ctrl1_shadow = 0;
  _ctrl2_shadow = 0;
  _ctrl3_shadow = 0;
  _bandwidth_code = reg::BW_100_HZ;
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
  _last_error = Error::MeasurementTimeout;
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
// Magnetic measurement -- synchronous
// ---------------------------------------------------------------------------

bool MMC5983MA::readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z) {
  _last_error = Error::None;
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

bool MMC5983MA::readMagneticAxisRaw(Axis axis, uint32_t* value) {
  _last_error = Error::None;
  if (value == nullptr) {
    _last_error = Error::InvalidArgument;
    return false;
  }
  uint32_t x, y, z;
  if (!readMagneticRaw(&x, &y, &z)) {
    return false;
  }
  switch (axis) {
    case Axis::X: *value = x; return true;
    case Axis::Y: *value = y; return true;
    case Axis::Z: *value = z; return true;
  }
  _last_error = Error::InvalidArgument;
  return false;
}

bool MMC5983MA::readMagneticAxisUT(Axis axis, float* value) {
  uint32_t raw;
  if (!readMagneticAxisRaw(axis, &raw)) {
    return false;
  }
  if (value == nullptr) {
    _last_error = Error::InvalidArgument;
    return false;
  }
  *value = toMicrotesla(raw);
  return true;
}

bool MMC5983MA::readMagneticOffsetCancelled(float* x, float* y, float* z) {
  _last_error = Error::None;
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
// Magnetic measurement -- async
// ---------------------------------------------------------------------------

bool MMC5983MA::triggerSingleShotRead() {
  _last_error = Error::None;
  if (_ctrl2_shadow & reg::CTRL2_CMM_EN) {
    // Continuous mode is already self-triggering. No-op, succeeds.
    return true;
  }
  setCoil();
  if (!writeRegister(reg::REG_STATUS, reg::STATUS_MEAS_M_DONE)) {
    return false;
  }
  return writeRegister(reg::REG_INT_CTRL_0,
                       (uint8_t)(_ctrl0_shadow | reg::CTRL0_TM_M));
}

bool MMC5983MA::readLatestMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z) {
  _last_error = Error::None;
  return readRawXYZ(x, y, z);
}

bool MMC5983MA::readLatestMagneticUT(float* x, float* y, float* z) {
  uint32_t rx, ry, rz;
  if (!readLatestMagneticRaw(&rx, &ry, &rz)) {
    return false;
  }
  *x = toMicrotesla(rx);
  *y = toMicrotesla(ry);
  *z = toMicrotesla(rz);
  return true;
}

// ---------------------------------------------------------------------------
// Temperature
// ---------------------------------------------------------------------------

float MMC5983MA::readTemperatureC() {
  _last_error = Error::None;
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
  _last_error = Error::MeasurementTimeout;
  return NAN;
}

// ---------------------------------------------------------------------------
// SET / RESET coil control
// ---------------------------------------------------------------------------

void MMC5983MA::setCoil() {
  _last_error = Error::None;
  writeRegister(reg::REG_INT_CTRL_0,
                (uint8_t)(_ctrl0_shadow | reg::CTRL0_SET));
  delay(reg::SET_RESET_PULSE_DELAY_MS);
}

void MMC5983MA::resetCoil() {
  _last_error = Error::None;
  writeRegister(reg::REG_INT_CTRL_0,
                (uint8_t)(_ctrl0_shadow | reg::CTRL0_RESET));
  delay(reg::SET_RESET_PULSE_DELAY_MS);
}

void MMC5983MA::setAutomaticSetReset(bool enabled) {
  _last_error = Error::None;
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
  _last_error = Error::None;
  uint8_t code;
  switch (hz) {
    case 100: code = reg::BW_100_HZ; break;
    case 200: code = reg::BW_200_HZ; break;
    case 400: code = reg::BW_400_HZ; break;
    case 800: code = reg::BW_800_HZ; break;
    default:
      _last_error = Error::InvalidArgument;
      return false;
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
  _last_error = Error::None;
  uint8_t freq_code;
  switch (rate_hz) {
    case 1:    freq_code = reg::CM_FREQ_1_HZ; break;
    case 10:   freq_code = reg::CM_FREQ_10_HZ; break;
    case 20:   freq_code = reg::CM_FREQ_20_HZ; break;
    case 50:   freq_code = reg::CM_FREQ_50_HZ; break;
    case 100:  freq_code = reg::CM_FREQ_100_HZ; break;
    case 200:  freq_code = reg::CM_FREQ_200_HZ; break;
    case 1000: freq_code = reg::CM_FREQ_1000_HZ; break;
    default:
      _last_error = Error::InvalidArgument;
      return false;
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
  _last_error = Error::MeasurementTimeout;
  return false;
}

void MMC5983MA::stopContinuousMode() {
  _last_error = Error::None;
  _ctrl2_shadow = (uint8_t)(_ctrl2_shadow
                            & ~(reg::CTRL2_CMM_EN | reg::CTRL2_CM_FREQ_MASK));
  writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

bool MMC5983MA::isContinuousModeEnabled() const {
  return (_ctrl2_shadow & reg::CTRL2_CMM_EN) != 0;
}

bool MMC5983MA::isDataReady() {
  _last_error = Error::None;
  uint8_t status;
  if (!readRegister(reg::REG_STATUS, &status)) {
    return false;
  }
  return (status & reg::STATUS_MEAS_M_DONE) != 0;
}

// ---------------------------------------------------------------------------
// Channel enables
// ---------------------------------------------------------------------------

void MMC5983MA::setXEnabled(bool enabled) {
  _last_error = Error::None;
  if (enabled) {
    _ctrl1_shadow = (uint8_t)(_ctrl1_shadow & ~reg::CTRL1_X_INHIBIT);
  } else {
    _ctrl1_shadow |= reg::CTRL1_X_INHIBIT;
  }
  writeRegister(reg::REG_INT_CTRL_1, _ctrl1_shadow);
}

bool MMC5983MA::isXEnabled() const {
  return (_ctrl1_shadow & reg::CTRL1_X_INHIBIT) == 0;
}

void MMC5983MA::setYZEnabled(bool enabled) {
  _last_error = Error::None;
  if (enabled) {
    _ctrl1_shadow = (uint8_t)(_ctrl1_shadow & ~reg::CTRL1_YZ_INHIBIT);
  } else {
    _ctrl1_shadow |= reg::CTRL1_YZ_INHIBIT;
  }
  writeRegister(reg::REG_INT_CTRL_1, _ctrl1_shadow);
}

bool MMC5983MA::isYZEnabled() const {
  return (_ctrl1_shadow & reg::CTRL1_YZ_INHIBIT) == 0;
}

// ---------------------------------------------------------------------------
// Periodic SET
// ---------------------------------------------------------------------------

bool MMC5983MA::setPeriodicSet(uint16_t sample_count) {
  _last_error = Error::None;
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
    default:
      _last_error = Error::InvalidArgument;
      return false;
  }
  _ctrl2_shadow = (uint8_t)((_ctrl2_shadow & ~reg::CTRL2_PRD_SET_MASK)
                            | (code << reg::CTRL2_PRD_SET_SHIFT)
                            | reg::CTRL2_EN_PRD_SET);
  return writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

void MMC5983MA::disablePeriodicSet() {
  _last_error = Error::None;
  _ctrl2_shadow = (uint8_t)(_ctrl2_shadow
                            & ~(reg::CTRL2_EN_PRD_SET | reg::CTRL2_PRD_SET_MASK));
  writeRegister(reg::REG_INT_CTRL_2, _ctrl2_shadow);
}

// ---------------------------------------------------------------------------
// Interrupts and SPI mode
// ---------------------------------------------------------------------------

void MMC5983MA::setInterruptEnabled(bool enabled) {
  _last_error = Error::None;
  if (enabled) {
    _ctrl0_shadow |= reg::CTRL0_INT_MEAS_DONE_EN;
  } else {
    _ctrl0_shadow = (uint8_t)(_ctrl0_shadow & ~reg::CTRL0_INT_MEAS_DONE_EN);
  }
  writeRegister(reg::REG_INT_CTRL_0, _ctrl0_shadow);
}

void MMC5983MA::setInterruptDataReadyPin(uint8_t pin, void (*callback)()) {
  _last_error = Error::None;
  pinMode(pin, INPUT);
  if (callback != nullptr) {
    attachInterrupt(digitalPinToInterrupt(pin), callback, RISING);
  }
  setInterruptEnabled(true);
}

void MMC5983MA::clearInterrupt(uint8_t mask) {
  _last_error = Error::None;
  writeRegister(reg::REG_STATUS, mask);
}

void MMC5983MA::setSpi3Wire(bool enabled) {
  _last_error = Error::None;
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
  _last_error = Error::None;
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
