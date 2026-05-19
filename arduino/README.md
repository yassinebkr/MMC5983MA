# MMC5983MA — Arduino library

An Arduino library for the [MEMSIC MMC5983MA](https://www.memsic.com/magnetometer-2)
3-axis magnetometer with 18-bit resolution and ±8 gauss full-scale range.

Supports both I2C and SPI on Arduino-compatible boards. Returns microtesla
directly, offers offset-cancelled reads using the SET/RESET coils, exposes
the on-die self-test, and handles two hardware quirks (SET pulse before
single-shot reads; first-sample wait when entering continuous mode) that
the SparkFun reference library leaves to the caller.

Hardware-validated on the
[Adafruit Feather RP2040 RFM95](https://www.adafruit.com/product/5714)
with the Earle Philhower arduino-pico core.

## Quickstart — I2C

```cpp
#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  if (!mag.begin()) {
    Serial.println("MMC5983MA not found");
    while (true) delay(1000);
  }
}

void loop() {
  float x, y, z;
  if (mag.readMagneticUT(&x, &y, &z)) {
    Serial.print("X="); Serial.print(x, 2);
    Serial.print(" Y="); Serial.print(y, 2);
    Serial.print(" Z="); Serial.print(z, 2);
    Serial.println(" uT");
  }
  delay(100);
}
```

## Quickstart — SPI

```cpp
#include <SPI.h>
#include <MMC5983MA.h>

static const uint8_t CS_PIN = 7;  // D5 on Feather RP2040

MMC5983MA mag(SPI, CS_PIN, 8000000UL);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  if (!mag.begin()) {
    Serial.println("MMC5983MA not found");
    while (true) delay(1000);
  }
}

void loop() {
  float x, y, z;
  if (mag.readMagneticUT(&x, &y, &z)) {
    Serial.print("X="); Serial.print(x, 2);
    Serial.print(" Y="); Serial.print(y, 2);
    Serial.print(" Z="); Serial.print(z, 2);
    Serial.println(" uT");
  }
  delay(100);
}
```

## Examples

| Example            | Bus | What it does                                                  |
|--------------------|-----|---------------------------------------------------------------|
| `BasicI2C`         | I2C | Single-shot reads at 10 Hz with self-test on startup          |
| `BasicSPI`         | SPI | Same as `BasicI2C` over SPI                                   |
| `Selftest`         | I2C | Run the on-die saturation self-test and print verdict         |
| `ContinuousMode`   | I2C | Stream 100 Hz with automatic SET/RESET for 30 s               |
| `Calibration`      | I2C | Collect hard-iron offsets while rotating the sensor           |
| `HeadingCompass`   | I2C | 2D compass heading from the X/Y components                   |
| `InterruptDriven`  | I2C | INT-pin-driven reads using the chip's MEAS_DONE interrupt     |
| `AsyncRead`        | I2C | Async trigger / wait / read so the CPU can do other work      |

Open them from **File → Examples → MMC5983MA** in the Arduino IDE.

## What's in 0.2.0

- `lastError()` returns a `MMC5983MA::Error` enum after any false / NAN
  return, so you can distinguish `BusNack`, `BusTimeout`,
  `MeasurementTimeout`, `IdMismatch`, and `InvalidArgument`.
- Async trigger / read API: `triggerSingleShotRead()` +
  `readLatestMagneticUT/Raw()` so the CPU is free during the chip's
  measurement window.
- Per-axis reads: `readMagneticAxisUT(Axis::X, &value)` and
  `readMagneticAxisRaw`.
- Per-channel power saving: `setXEnabled(bool)` and `setYZEnabled(bool)`.
- `MMC5983MA(SPIClass&, cs, SPISettings)` constructor and
  `setSpiSettings()` setter for full control over SPI mode / bit order /
  clock when sharing the bus with peripherals.
- `setInterruptDataReadyPin(pin, callback)` configures both the chip's
  INT pin and the host-side `attachInterrupt()` in one call.
- Hot-path bus methods (`readRegister` / `readBurst` / `writeRegister`)
  are now `inline` in the header so the compiler can fold them into the
  rest of the driver with no call boundary.

## Installation

See [`docs/INSTALL.md`](docs/INSTALL.md) for board core install, wiring
tables (I2C STEMMA QT and SPI with CS pin), and a first-run checklist.

## API reference

See [`docs/API_REFERENCE.md`](docs/API_REFERENCE.md). Two hardware quirks
the driver handles automatically are called out at the bottom of that
document.

## Design notes

The library follows the patterns proven in the companion CircuitPython
port at [`../circuitpython/`](../circuitpython/):

- **Registers separated** from the driver in
  [`src/MMC5983MA_Registers.h`](src/MMC5983MA_Registers.h). Every address
  and bit field is a `constexpr` in `namespace mmc5983ma`; nothing in the
  driver `.cpp` is a magic number.
- **Pre-allocated buffer** for the 7-byte data read, so the hot path
  performs no heap allocations.
- **Both bus paths are first-class.** `readRegister`, `readBurst`, and
  `writeRegister` each branch on whether the I2C or SPI constructor was
  used; the SPI branch handles CS toggling, the address MSB convention
  for read vs. write, and `SPISettings`-wrapped transactions.
- **Failure-explicit API.** Every operation that can fail returns `bool`
  (or `NAN` for `readTemperatureC`). No silent zeros.

## License

MIT — see [`../LICENSE`](../LICENSE).
