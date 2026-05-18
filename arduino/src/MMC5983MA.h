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
//   bus failure or measurement timeout is visible to the caller.
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
  // Construct an I2C-backed driver. The TwoWire instance must already be
  // begin()'d before begin() is called on the driver.
  explicit MMC5983MA(TwoWire& wire = Wire, uint8_t address = mmc5983ma::I2C_ADDRESS);

  // Construct an SPI-backed driver. The SPIClass instance must already be
  // begin()'d before begin() is called on the driver. cs_pin is configured
  // as an output and driven high in begin(). Datasheet allows up to 10 MHz.
  MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz = 8000000UL);

  // Verify the product ID and issue a software reset. Returns true on
  // success; false if the bus is unresponsive or PROD_ID does not match.
  bool begin();

  // True iff the chip responds with the expected PROD_ID.
  bool isConnected();

  // Issue a software reset and wait ~15 ms for the chip to restore defaults.
  // Also clears the host-side shadow state so it matches the device.
  void reset();

  // --------------------------------------------------------------------
  // Magnetic measurement
  // --------------------------------------------------------------------

  // Read the magnetic field in microtesla. In single-shot mode this fires
  // a SET pulse, triggers a measurement, and waits for completion. In
  // continuous mode it returns the latest data-register sample directly.
  // Returns false on bus failure or measurement timeout.
  bool readMagneticUT(float* x, float* y, float* z);

  // Same as readMagneticUT but returns the raw 18-bit unsigned counts.
  bool readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z);

  // Offset-cancelled read: takes a SET measurement and a RESET measurement,
  // returns (M_set - M_reset) / 2 per axis in microtesla. Cancels the slow
  // internal offset drift that is the dominant systematic error in this
  // part, at the cost of two measurement cycles per sample.
  bool readMagneticOffsetCancelled(float* x, float* y, float* z);

  // Die temperature in degrees Celsius. Always single-shot. Returns NAN
  // on bus failure or measurement timeout.
  float readTemperatureC();

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

  // Enable/disable the INT pin asserting on measurement-done.
  void setInterruptEnabled(bool enabled);

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
  // ----- Bus abstraction (real implementations in MMC5983MA.cpp) ---------

  bool readRegister(uint8_t address, uint8_t* value);
  bool readBurst(uint8_t address, uint8_t* buffer, size_t length);
  bool writeRegister(uint8_t address, uint8_t value);

  // ----- Internal helpers ------------------------------------------------

  bool triggerMagneticAndWait();
  bool readRawXYZ(uint32_t* x, uint32_t* y, uint32_t* z);
  uint16_t measurementTimeoutMs() const;
  static float toMicrotesla(uint32_t raw);

  // ----- Bus state -------------------------------------------------------

  // Exactly one of _wire / _spi is non-null. _wire selects I2C, _spi
  // selects SPI.
  TwoWire*  _wire;
  SPIClass* _spi;
  uint8_t   _i2c_address;
  uint8_t   _cs_pin;
  uint32_t  _spi_clock_hz;

  // ----- Shadow registers and pre-allocated buffer -----------------------

  uint8_t _magBuf[7];
  uint8_t _ctrl0_shadow;
  uint8_t _ctrl1_shadow;
  uint8_t _ctrl2_shadow;
  uint8_t _ctrl3_shadow;
  uint8_t _bandwidth_code;
};

#endif  // MMC5983MA_H
