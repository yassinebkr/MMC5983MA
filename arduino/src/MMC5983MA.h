// Driver for the MEMSIC MMC5983MA 3-axis magnetometer.
//
// Supports both I2C (Wire) and SPI buses through a single class. The two
// constructors select which bus the instance talks to; every public method
// is implemented identically for either bus.
//
// Architectural notes:
// - Configuration registers are tracked as host-side shadows because several
//   bits (SET, RESET, TM_M, TM_T, SW_RST) are transient triggers that
//   self-clear, so reading them back would not give a meaningful state.
// - The 7-byte magnetic data buffer is pre-allocated as a class member so
//   the hot path performs no heap allocations.
// - Public reads return bool and write results via pointer arguments so a
//   bus failure or measurement timeout is visible to the caller; a more
//   specific reason is available via lastError().
// - The three bus-abstraction methods (readRegister, readBurst,
//   writeRegister) are defined inline at the bottom of this header so the
//   compiler can inline them into the rest of the driver, with no .cpp
//   call boundary in the hot path.
// - Mirrors the hardware-validated CircuitPython port at
//   ../../circuitpython/mmc5983ma/__init__.py.

#ifndef MMC5983MA_H
#define MMC5983MA_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "MMC5983MA_Registers.h"

class MMC5983MA {
 public:
  // Reason the last public call failed. None when the most recent call
  // succeeded. Sticky between calls: query immediately after a false
  // (or NAN) return.
  enum class Error : uint8_t {
    None = 0,
    BusTimeout,           // I2C / SPI transaction timed out at the bus layer
    BusNack,              // I2C slave NACKed an address or data byte
    IdMismatch,           // PROD_ID register did not return the expected value
    MeasurementTimeout,   // status MEAS_M_DONE never asserted within the window
    InvalidArgument,      // out-of-range argument (bandwidth, rate, axis, ...)
  };

  // Identifies one of the three magnetic-field axes, for the per-axis read
  // helpers. The integer value matches the order the chip emits in its
  // data registers (X=0, Y=1, Z=2).
  enum class Axis : uint8_t {
    X = 0,
    Y = 1,
    Z = 2,
  };

  // Construct an I2C-backed driver. The TwoWire instance must already be
  // begin()'d before begin() is called on the driver.
  explicit MMC5983MA(TwoWire& wire = Wire, uint8_t address = mmc5983ma::I2C_ADDRESS);

  // Construct an SPI-backed driver. The SPIClass instance must already be
  // begin()'d before begin() is called on the driver. cs_pin is configured
  // as an output and driven high in begin(). Datasheet allows up to 10 MHz.
  MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz = 8000000UL);

  // Construct an SPI-backed driver with a fully custom SPISettings (mode,
  // bit order, clock). Useful when sharing the bus with peripherals that
  // need a different mode.
  MMC5983MA(SPIClass& spi, uint8_t cs_pin, SPISettings settings);

  // Replace the SPISettings used for every SPI transaction. No effect on
  // an I2C-backed instance.
  void setSpiSettings(SPISettings settings);

  // Verify the product ID and issue a software reset. Returns true on
  // success; false if the bus is unresponsive or PROD_ID does not match
  // (check lastError() to distinguish).
  bool begin();

  // True iff the chip responds with the expected PROD_ID.
  bool isConnected();

  // Issue a software reset and wait ~15 ms for the chip to restore defaults.
  // Also clears the host-side shadow state so it matches the device.
  void reset();

  // Reason the last public call failed, or Error::None if it succeeded.
  Error lastError() const { return _last_error; }

  // --------------------------------------------------------------------
  // Magnetic measurement -- synchronous (all in one call)
  // --------------------------------------------------------------------

  // Read the magnetic field in microtesla. In single-shot mode this fires
  // a SET pulse, triggers a measurement, and waits for completion. In
  // continuous mode it returns the latest data-register sample directly.
  // Returns false on bus failure or measurement timeout.
  bool readMagneticUT(float* x, float* y, float* z);

  // Same as readMagneticUT but returns the raw 18-bit unsigned counts.
  bool readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z);

  // Read one axis only. Does a full 7-byte burst under the hood because
  // the chip packs all three axes' LSBs into the shared XYZ_OUT_2
  // register, so this is API convenience, not a bandwidth saving.
  bool readMagneticAxisUT(Axis axis, float* value);
  bool readMagneticAxisRaw(Axis axis, uint32_t* value);

  // Offset-cancelled read: takes a SET measurement and a RESET measurement,
  // returns (M_set - M_reset) / 2 per axis in microtesla. Cancels the slow
  // internal offset drift that is the dominant systematic error in this
  // part, at the cost of two measurement cycles per sample.
  bool readMagneticOffsetCancelled(float* x, float* y, float* z);

  // Die temperature in degrees Celsius. Always single-shot. Returns NAN
  // on bus failure or measurement timeout.
  float readTemperatureC();

  // --------------------------------------------------------------------
  // Magnetic measurement -- async (split trigger / read)
  // --------------------------------------------------------------------

  // Fire a SET pulse and trigger a single-shot magnetic measurement,
  // returning immediately. The caller is responsible for polling
  // isDataReady() (or wiring INT, see setInterruptDataReadyPin) before
  // calling readLatestMagneticUT / readLatestMagneticRaw. Useful for
  // interleaving the magnetometer's measurement window with other work
  // (e.g. reading an IMU on a parallel bus). Has no effect when the chip
  // is in continuous mode (the chip is already triggering itself).
  bool triggerSingleShotRead();

  // Read the current contents of the data registers without triggering a
  // new measurement. Pairs with triggerSingleShotRead, or with continuous
  // mode where the chip refreshes the data registers on its own schedule.
  bool readLatestMagneticUT(float* x, float* y, float* z);
  bool readLatestMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z);

  // --------------------------------------------------------------------
  // SET / RESET coil control
  // --------------------------------------------------------------------

  // Fire the SET coil pulse (magnetizes the internal element positive).
  // Blocks SET_RESET_PULSE_DELAY_MS for the pulse to settle.
  void setCoil();

  // Fire the RESET coil pulse (magnetizes the internal element negative).
  // Blocks SET_RESET_PULSE_DELAY_MS for the pulse to settle.
  void resetCoil();

  // Enable/disable the chip's automatic SET/RESET in continuous mode.
  // Recommended for long-running continuous operation.
  void setAutomaticSetReset(bool enabled);

  // --------------------------------------------------------------------
  // Bandwidth and continuous mode
  // --------------------------------------------------------------------

  // Set filter bandwidth. Valid values: 100, 200, 400, 800 (Hz).
  // Returns false on invalid input.
  bool setBandwidth(uint16_t hz);

  // Current bandwidth in Hz.
  uint16_t getBandwidth() const;

  // Start continuous-measurement mode at rate_hz. Valid rates: 1, 10, 20,
  // 50, 100, 200, 1000. Promotes bandwidth automatically when the rate
  // requires it (>=200 Hz needs >=200 Hz BW; 1000 Hz needs 800 Hz BW).
  //
  // BLOCKS until the first sample is in the data registers. Without this
  // wait, the first read after enabling continuous mode hits all-zero
  // registers and reports |B|=1419 uT (= sqrt(3)*offset*sensitivity).
  bool startContinuousMode(uint16_t rate_hz, bool automatic_set_reset = true);

  // Disable continuous mode and return to one-shot triggering.
  void stopContinuousMode();

  // True if continuous mode is enabled in the host-side shadow.
  bool isContinuousModeEnabled() const;

  // True if STATUS.MEAS_M_DONE is asserted on the chip.
  bool isDataReady();

  // --------------------------------------------------------------------
  // Channel enables (power / time saver)
  // --------------------------------------------------------------------

  // Enable / disable the X channel. A disabled channel does not consume
  // measurement time or power. Default is enabled.
  void setXEnabled(bool enabled);
  bool isXEnabled() const;

  // Enable / disable the Y and Z channels (they share an inhibit bit
  // pair in the chip, so they toggle together). Default is enabled.
  void setYZEnabled(bool enabled);
  bool isYZEnabled() const;

  // --------------------------------------------------------------------
  // Periodic SET
  // --------------------------------------------------------------------

  // Enable periodic SET every sample_count measurements. Valid counts:
  // 1, 25, 75, 100, 250, 500, 1000, 2000. Smaller counts give better
  // offset stability at the cost of more pulses.
  bool setPeriodicSet(uint16_t sample_count);

  // Disable periodic SET.
  void disablePeriodicSet();

  // --------------------------------------------------------------------
  // Interrupts and SPI mode
  // --------------------------------------------------------------------

  // Enable/disable the INT pin asserting on measurement-done. Configures
  // only the chip side; wire your own attachInterrupt() on the host, or
  // call setInterruptDataReadyPin below to do both at once.
  void setInterruptEnabled(bool enabled);

  // Configure the host GPIO connected to the chip's INT pin and attach
  // a user-supplied callback (typically a function that sets a volatile
  // flag the main loop polls). Enables the chip's INT pin as a side
  // effect. Use clearInterrupt() in or after the callback to allow the
  // chip to fire INT again.
  void setInterruptDataReadyPin(uint8_t pin, void (*callback)());

  // Clear the named status flags by writing 1s to them. Defaults to
  // clearing both magnetic and temperature data-ready flags.
  void clearInterrupt(uint8_t mask = mmc5983ma::STATUS_MEAS_M_DONE | mmc5983ma::STATUS_MEAS_T_DONE);

  // Switch the SPI port to 3-wire mode (bidirectional SDIO). Has no
  // effect over I2C.
  void setSpi3Wire(bool enabled);

  // --------------------------------------------------------------------
  // Self-test
  // --------------------------------------------------------------------

  // Run the built-in saturation self-test. Returns true when every axis
  // shows a deflection larger than SELFTEST_THRESHOLD_UT between ST_ENP
  // and ST_ENM measurements. A false result usually means the sensor is
  // in a strong DC field; check by moving it 30 cm clear of ferrous
  // objects. Returns false on bus failure as well.
  bool runSelftest();

 private:
  // ----- Internal helpers ------------------------------------------------

  bool triggerMagneticAndWait();
  bool readRawXYZ(uint32_t* x, uint32_t* y, uint32_t* z);
  uint16_t measurementTimeoutMs() const;
  static float toMicrotesla(uint32_t raw);

  // ----- Bus state -------------------------------------------------------

  // Exactly one of _wire / _spi is non-null. _wire selects I2C, _spi
  // selects SPI.
  TwoWire*    _wire;
  SPIClass*   _spi;
  uint8_t     _i2c_address;
  uint8_t     _cs_pin;
  SPISettings _spi_settings;

  // ----- Shadow registers, pre-allocated buffer, last error --------------

  uint8_t _magBuf[7];
  uint8_t _ctrl0_shadow;
  uint8_t _ctrl1_shadow;
  uint8_t _ctrl2_shadow;
  uint8_t _ctrl3_shadow;
  uint8_t _bandwidth_code;
  Error   _last_error;

  // ----- Inline bus abstraction (see header comment) ---------------------

  inline bool readRegister(uint8_t address, uint8_t* value) {
    if (_wire != nullptr) {
      _wire->beginTransmission(_i2c_address);
      _wire->write(address);
      if (_wire->endTransmission(false) != 0) {
        _last_error = Error::BusNack;
        return false;
      }
      if (_wire->requestFrom((uint8_t)_i2c_address, (uint8_t)1) != 1) {
        _last_error = Error::BusTimeout;
        return false;
      }
      *value = _wire->read();
      return true;
    }
    _spi->beginTransaction(_spi_settings);
    digitalWrite(_cs_pin, LOW);
    _spi->transfer((uint8_t)(address | mmc5983ma::SPI_READ));
    *value = _spi->transfer(0x00);
    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();
    return true;
  }

  inline bool readBurst(uint8_t address, uint8_t* buffer, size_t length) {
    if (_wire != nullptr) {
      _wire->beginTransmission(_i2c_address);
      _wire->write(address);
      if (_wire->endTransmission(false) != 0) {
        _last_error = Error::BusNack;
        return false;
      }
      size_t got = _wire->requestFrom((uint8_t)_i2c_address, (uint8_t)length);
      if (got != length) {
        _last_error = Error::BusTimeout;
        return false;
      }
      for (size_t i = 0; i < length; ++i) {
        buffer[i] = _wire->read();
      }
      return true;
    }
    _spi->beginTransaction(_spi_settings);
    digitalWrite(_cs_pin, LOW);
    _spi->transfer((uint8_t)(address | mmc5983ma::SPI_READ));
    for (size_t i = 0; i < length; ++i) {
      buffer[i] = _spi->transfer(0x00);
    }
    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();
    return true;
  }

  inline bool writeRegister(uint8_t address, uint8_t value) {
    if (_wire != nullptr) {
      _wire->beginTransmission(_i2c_address);
      _wire->write(address);
      _wire->write(value);
      if (_wire->endTransmission() != 0) {
        _last_error = Error::BusNack;
        return false;
      }
      return true;
    }
    _spi->beginTransaction(_spi_settings);
    digitalWrite(_cs_pin, LOW);
    _spi->transfer((uint8_t)(address & 0x7F));
    _spi->transfer(value);
    digitalWrite(_cs_pin, HIGH);
    _spi->endTransaction();
    return true;
  }
};

#endif  // MMC5983MA_H
