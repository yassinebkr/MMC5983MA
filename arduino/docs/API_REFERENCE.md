# API reference — `MMC5983MA`

## Nested types

### `enum class MMC5983MA::Error : uint8_t`

Reason the last public call failed. See `lastError()`. Values:

| Value                  | Meaning                                                    |
|------------------------|------------------------------------------------------------|
| `None`                 | Most recent call succeeded.                                |
| `BusTimeout`           | I2C or SPI transaction did not return the expected bytes.  |
| `BusNack`              | I2C slave NACKed an address or data byte.                  |
| `IdMismatch`           | `REG_PROD_ID` did not return the expected `0x30`.          |
| `MeasurementTimeout`   | `STATUS.MEAS_M_DONE` never asserted within the BW window.  |
| `InvalidArgument`      | Out-of-range argument (bandwidth, rate, axis enum, …).     |

### `enum class MMC5983MA::Axis : uint8_t`

`X = 0`, `Y = 1`, `Z = 2`. Used by `readMagneticAxisUT/Raw`.

## Constructors

### `MMC5983MA(TwoWire& wire = Wire, uint8_t address = 0x30)`

I2C-backed driver. `wire` must already be `begin()`'d before
`MMC5983MA::begin()` is called. `address` is fixed at `0x30` on the part.

### `MMC5983MA(SPIClass& spi, uint8_t cs_pin, uint32_t spi_clock_hz = 8000000)`

SPI-backed driver. `spi` must already be `begin()`'d. `cs_pin` is
configured as an output and driven HIGH in `begin()`. The datasheet allows
clocks up to 10 MHz. SPI mode defaults to `SPI_MODE0`, MSB first.

### `MMC5983MA(SPIClass& spi, uint8_t cs_pin, SPISettings settings)`

Same as the previous SPI constructor but accepts a fully custom
`SPISettings` for non-default mode / bit order / clock. Useful when
sharing the SPI bus with peripherals that need a different mode.

### `void setSpiSettings(SPISettings settings)`

Replace the `SPISettings` used for every subsequent SPI transaction. No
effect on an I2C-backed instance.

## Lifecycle

### `bool begin()`

Verifies the product ID and issues a software reset. Returns `false` if
the bus is unresponsive or the chip's PROD_ID does not match. Check
`lastError()` to distinguish `BusNack` / `BusTimeout` from `IdMismatch`.

### `bool isConnected()`

`true` iff the chip responds with the expected PROD_ID.

### `void reset()`

Software reset. Blocks ~15 ms while the chip restores defaults, then
clears the driver's shadow state so it matches the device.

### `Error lastError() const`

Reason the most recent public call failed, or `Error::None` if it
succeeded. Every public method that touches the bus clears this on
entry and sets it on the failure path, so it always reflects the last
call's outcome. Query immediately after a `false` (or `NAN`) return.

## Magnetic measurement

### `bool readMagneticUT(float* x, float* y, float* z)`

Magnetic field in **microtesla**. In single-shot mode this fires a SET
pulse, triggers a measurement, and waits for completion. In continuous
mode it returns the latest data-register sample directly. Returns `false`
on bus failure or measurement timeout.

### `bool readMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z)`

Raw 18-bit unsigned values (`0..0x3FFFF`). `0x20000` is zero field.
Returns `false` on the same failure modes as `readMagneticUT`.

### `bool readMagneticAxisUT(Axis axis, float* value)` / `bool readMagneticAxisRaw(Axis axis, uint32_t* value)`

Read one axis only. Does a full 7-byte burst under the hood because the
chip packs all three axes' LSBs into the shared `XYZ_OUT_2` register,
so this is API convenience, not a bandwidth saving.

### `bool readMagneticOffsetCancelled(float* x, float* y, float* z)`

Takes a SET measurement and a RESET measurement, returns
`(M_set - M_reset) / 2` per axis in microtesla. Cancels the slow internal
offset drift that is the dominant systematic error in this part, at the
cost of two measurement cycles per sample.

## Async magnetic measurement

Split the synchronous single-shot read into a non-blocking trigger and a
later read, so the CPU is free during the chip's ~1-10 ms measurement
window (depending on bandwidth).

### `bool triggerSingleShotRead()`

Fire a SET pulse and trigger a single-shot measurement, returning
immediately. The caller polls `isDataReady()` (or wires an INT pin and
uses `setInterruptDataReadyPin()`) before reading. No-op and returns
`true` when continuous mode is already enabled.

### `bool readLatestMagneticUT(float* x, float* y, float* z)` / `bool readLatestMagneticRaw(uint32_t* x, uint32_t* y, uint32_t* z)`

Read the current contents of the data registers without triggering a
new measurement. Pairs with `triggerSingleShotRead()` or with continuous
mode.

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

## Channel enables

### `void setXEnabled(bool enabled)` / `bool isXEnabled() const`

Enable or disable the X channel via `CTRL1_X_INHIBIT`. A disabled
channel does not consume measurement time or power. Default is enabled.

### `void setYZEnabled(bool enabled)` / `bool isYZEnabled() const`

Enable or disable the Y and Z channels together. The chip has a single
register field for both, so they toggle as a pair. Default is enabled.

## Periodic SET

### `bool setPeriodicSet(uint16_t sample_count)`

Fire a SET pulse every `sample_count` measurements. Valid counts:
`1, 25, 75, 100, 250, 500, 1000, 2000`. Smaller counts give better offset
stability at the cost of more pulses. Returns `false` on invalid input.

### `void disablePeriodicSet()`

Turn off periodic SET.

## Interrupts and SPI mode

### `void setInterruptEnabled(bool enabled)`

Drive the `INT` pin HIGH on measurement-done. Configures only the chip
side. Use `setInterruptDataReadyPin()` to also wire the host-side
`attachInterrupt()` in one call.

### `void setInterruptDataReadyPin(uint8_t pin, void (*callback)())`

Configure the host GPIO connected to the chip's INT pin and attach a
user-supplied callback (typically a function that sets a `volatile bool`
flag the main loop polls). Enables the chip's INT-on-done bit as a side
effect. Call `clearInterrupt()` in or after the callback so the chip
can fire INT again on the next sample.

```cpp
volatile bool ready = false;
void onReady() { ready = true; }
// ...
mag.setInterruptDataReadyPin(A0, onReady);
```

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
