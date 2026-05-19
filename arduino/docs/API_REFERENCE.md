# API reference — `MMC5983MA`

## Constructors

### `MMC5983MA(TwoWire& wire = Wire, uint8_t address = 0x30)`

I2C-backed driver. `wire` must already be `begin()`'d before
`MMC5983MA::begin()` is called. `address` is fixed at `0x30` on the part.

### `MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz = 8000000)`

SPI-backed driver. `spi` must already be `begin()`'d. `cs_pin` is
configured as an output and driven HIGH in `begin()`. The datasheet allows
clocks up to 10 MHz.

## Lifecycle

### `bool begin()`

Verifies the product ID and issues a software reset. Returns `false` if
the bus is unresponsive or the chip's PROD_ID does not match.

### `bool isConnected()`

`true` iff the chip responds with the expected PROD_ID.

### `void reset()`

Software reset. Blocks ~15 ms while the chip restores defaults, then
clears the driver's shadow state so it matches the device.

## Magnetic measurement

### `bool readMagneticUT(float* x, float* y, float* z)`

Magnetic field in **microtesla**. In single-shot mode this fires a SET
pulse, triggers a measurement, and waits for completion. In continuous
mode it returns the latest data-register sample directly. Returns `false`
on bus failure or measurement timeout.

### `bool readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z)`

Raw 18-bit unsigned values (`0..0x3FFFF`). `0x20000` is zero field.
Returns `false` on the same failure modes as `readMagneticUT`.

### `bool readMagneticOffsetCancelled(float* x, float* y, float* z)`

Takes a SET measurement and a RESET measurement, returns
`(M_set - M_reset) / 2` per axis in microtesla. Cancels the slow internal
offset drift that is the dominant systematic error in this part, at the
cost of two measurement cycles per sample.

### `float readTemperatureC()`

Die temperature in degrees Celsius (–75 to +125 nominal). Always
single-shot. Returns `NAN` on bus failure or measurement timeout.

## SET / RESET coil control

### `void setCoil()` / `void resetCoil()`

Fire the SET or RESET coil pulse. Blocks 1 ms for the coil to settle.

### `void setAutomaticSetReset(bool enabled)`

Enable / disable the chip's automatic SET/RESET in continuous mode.
Recommended for long-running continuous operation.

## Bandwidth and continuous mode

### `bool setBandwidth(uint16_t hz)` / `uint16_t getBandwidth() const`

Filter bandwidth. Valid values: `100`, `200`, `400`, `800`. Higher is
faster and noisier. `setBandwidth` returns `false` on invalid input.

### `bool startContinuousMode(uint16_t rate_hz, bool automatic_set_reset = true)`

Start continuous mode. Valid `rate_hz`: `1, 10, 20, 50, 100, 200, 1000`.
The driver promotes bandwidth automatically when the rate requires it
(`>=200 Hz` needs `>=200 Hz` BW; `1000 Hz` needs `800 Hz` BW). **Blocks**
until the first sample is in the data registers (see "Two hardware
quirks" below).

### `void stopContinuousMode()`

Disable continuous mode and return to one-shot triggering.

### `bool isContinuousModeEnabled() const`

`true` if CMM_EN is set in the host-side shadow.

### `bool isDataReady()`

`true` if STATUS.MEAS_M_DONE is asserted on the chip.

## Periodic SET

### `bool setPeriodicSet(uint16_t sample_count)`

Fire a SET pulse every `sample_count` measurements. Valid counts:
`1, 25, 75, 100, 250, 500, 1000, 2000`. Smaller counts give better offset
stability at the cost of more pulses. Returns `false` on invalid input.

### `void disablePeriodicSet()`

Turn off periodic SET.

## Interrupts and SPI mode

### `void setInterruptEnabled(bool enabled)`

Drive the `INT` pin HIGH on measurement-done.

### `void clearInterrupt(uint8_t mask = STATUS_MEAS_M_DONE | STATUS_MEAS_T_DONE)`

Clear status flags by writing `1`s to them.

### `void setSpi3Wire(bool enabled)`

Switch the SPI port to 3-wire mode (bidirectional SDIO). No effect over
I2C.

## Self-test

### `bool runSelftest()`

Run the on-die saturation self-test. Returns `true` when every axis shows
a deflection larger than `SELFTEST_THRESHOLD_UT` (10 uT) between ST_ENP
and ST_ENM measurements. A `false` result usually means the sensor is in
a strong DC field; check by moving it 30 cm clear of ferrous objects.

## Two hardware quirks the driver handles for you

These are the two bugs the CircuitPython port caught on hardware that
mocked tests missed:

1. **SET pulse before single-shot reads.** Without a recent SET, the
   internal bridge polarity is undefined and `readMagneticUT` returns the
   chip's residual offset rather than the field (observed |B| ~11 uT
   instead of ~46 uT indoor). `readMagneticRaw` fires `setCoil()` before
   triggering each single-shot measurement when continuous mode is off.
2. **Continuous-mode startup race.** Reading data registers immediately
   after writing `CMM_EN` returns all zeros — the chip has not completed
   its first measurement yet. Computed |B| ~1419 uT
   (= √3 × offset × sensitivity). `startContinuousMode` clears
   `MEAS_M_DONE` then polls until the chip reasserts it before returning.

## Sensor specs (reference)

| Parameter            | Value                  |
|----------------------|------------------------|
| Resolution           | 18-bit                 |
| Full-scale range     | ±8 G (±800 uT)         |
| Sensitivity          | 0.0625 mG/LSB          |
| Output rate          | up to 1000 Hz          |
| Operating voltage    | 3.0–3.6 V              |
| I2C address          | `0x30` (fixed)         |
| SPI max clock        | 10 MHz                 |
| Self-test deflection | 80–175 mG per side     |
