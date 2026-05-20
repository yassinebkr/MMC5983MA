# MMC5983MA


A driver for the [MEMSIC MMC5983MA](https://www.memsic.com/magnetometer-2) 3-axis
magnetometer with 18-bit resolution and ±8 gauss full-scale range.

This repository hosts implementations for several embedded targets:

| Platform      | Status                                          | Path             |
|---------------|-------------------------------------------------|------------------|
| CircuitPython | Hardware-validated                              | `circuitpython/` |
| MicroPython   | Coming soon                                     | `micropython/`   |
| Arduino       | I2C hardware-validated; SPI compile-only        | `arduino/`       |

The CircuitPython driver was developed and validated on an
[Adafruit Feather RP2040 with RFM95 LoRa Radio](https://www.adafruit.com/product/5714)
with the sensor on the STEMMA QT bus. Bring-up reads the expected Earth
field (|B| ≈ 47 µT), self-test passes, and a 10-second continuous-mode
stress run shows |B| variation within 0.22 µT.

> **Contributing or releasing the Arduino library?** Read
> [`arduino/docs/TESTING.md`](arduino/docs/TESTING.md) first. It covers
> the CI checks that run on every PR, the local compile commands, and
> the `Verify020.ino` hardware bench test that must pass before tagging
> a release.

## Sensor at a glance

- **Resolution:** 18-bit (0.0625 mG / LSB nominal)
- **Range:** ±8 gauss (±800 µT)
- **Output rates:** up to 1000 Hz in continuous mode
- **Interfaces:** I²C (address `0x30`) and 4-wire / 3-wire SPI
- **Notable features:** SET/RESET coils for offset cancellation, on-die
  temperature sensor, programmable bandwidth filter, periodic SET, built-in
  self-test, automatic SET/RESET.

## Quickstart — I²C

### CircuitPython

```python
import board
import busio
from mmc5983ma import MMC5983MA

i2c = busio.I2C(board.SCL, board.SDA)
mag = MMC5983MA(i2c)

x, y, z = mag.magnetic
print(f"X={x:+.2f} µT  Y={y:+.2f} µT  Z={z:+.2f} µT")
```

### Arduino

```cpp
#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mag.begin();
}

void loop() {
  float x, y, z;
  if (mag.readMagneticUT(&x, &y, &z)) {
    Serial.print("X=");  Serial.print(x, 2);
    Serial.print(" Y="); Serial.print(y, 2);
    Serial.print(" Z="); Serial.print(z, 2);
    Serial.println(" uT");
  }
  delay(100);
}
```

### Browse more examples

- CircuitPython — [`circuitpython/examples/`](circuitpython/examples/) covers
  SPI, continuous mode, calibration, self-test, compass-heading, and a
  comprehensive [`hardware_bringup.py`](circuitpython/examples/hardware_bringup.py)
  that exercises every public path in one run.
- Arduino — [`arduino/examples/`](arduino/examples/) ships `BasicI2C`,
  `BasicSPI`, `Selftest`, `ContinuousMode`, `Calibration`, and
  `HeadingCompass` (each in its own folder for the Arduino IDE).

## Repository layout

```
/
├── README.md, LICENSE, pyproject.toml, requirements.txt, .pre-commit-config.yaml
├── circuitpython/
│   ├── mmc5983ma/        driver package (deploys to CIRCUITPY/lib/)
│   │   ├── __init__.py   driver class
│   │   └── registers.py  addresses, bit fields, timing constants
│   ├── examples/         basic_i2c, basic_spi, continuous_mode, calibration,
│   │                     selftest, heading_calculation, hardware_bringup
│   └── docs/             INSTALL.md, API_REFERENCE.md
├── arduino/              Arduino library (standard Arduino layout)
│   ├── library.properties, keywords.txt
│   ├── src/              MMC5983MA.h, MMC5983MA.cpp, MMC5983MA_Registers.h
│   ├── examples/         BasicI2C, BasicSPI, Selftest, ContinuousMode,
│   │                     Calibration, HeadingCompass
│   └── docs/             INSTALL.md, API_REFERENCE.md
├── tests/                pytest unit tests (run on CPython, not deployed)
│   ├── conftest.py       MockI2C fixture emulating the chip's register set
│   ├── test_basic_functionality.py
│   └── test_advanced_features.py
└── scripts/              host-side dev tools
    ├── repl_driver.py    drives the CircuitPython REPL over USB serial
    └── stress.py         10-second stability run, copy to CIRCUITPY root
```

## Running tests

Unit tests run on CPython against an in-process mock of the chip's
register set:

```bash
pip install pytest pyserial
python -m pytest tests/ -v
```

To validate against real hardware, copy `circuitpython/mmc5983ma/` to
`CIRCUITPY/lib/`, copy `circuitpython/examples/hardware_bringup.py` to
`CIRCUITPY/`, and run from the REPL:

```python
>>> import hardware_bringup as hb
>>> hb.test()
```

Or use [`scripts/repl_driver.py`](scripts/repl_driver.py) to drive the
REPL automatically over USB serial and capture the output.

## Installation

### CircuitPython

1. Flash CircuitPython 8.x or newer to your board.
2. Copy `circuitpython/mmc5983ma/` to `CIRCUITPY/lib/` so you end up
   with `CIRCUITPY/lib/mmc5983ma/__init__.py` and
   `CIRCUITPY/lib/mmc5983ma/registers.py`.
3. Copy any sketch from `circuitpython/examples/` to `CIRCUITPY/` as
   `code.py`.

Full wiring tables and a Feather RP2040–specific walkthrough are in
[`circuitpython/docs/INSTALL.md`](circuitpython/docs/INSTALL.md).

### Arduino

Pick one of the two install paths below. After install, restart the
Arduino IDE and the examples appear under
**File → Examples → MMC5983MA**.

**A. Arduino IDE — manual copy:**

1. Clone or download this repo.
2. Copy the entire `arduino/` directory into your Arduino libraries
   folder and rename it to `MMC5983MA`:
   - Windows: `%USERPROFILE%\Documents\Arduino\libraries\MMC5983MA\`
   - macOS: `~/Documents/Arduino/libraries/MMC5983MA/`
   - Linux: `~/Arduino/libraries/MMC5983MA/`
3. Restart the Arduino IDE.

**B. `arduino-cli` from this repo:**

```bash
arduino-cli lib install --git-url https://github.com/yassinebkr/MMC5983MA.git
```

You also need an Arduino core for your board. On the Feather RP2040
RFM95 that's the
[Earle Philhower arduino-pico core](https://github.com/earlephilhower/arduino-pico)
(`rp2040:rp2040:adafruit_feather_rfm`). Full board-core install steps,
I²C and SPI wiring tables, and a first-run checklist are in
[`arduino/docs/INSTALL.md`](arduino/docs/INSTALL.md).

## API reference

- CircuitPython — [`circuitpython/docs/API_REFERENCE.md`](circuitpython/docs/API_REFERENCE.md)
- Arduino — [`arduino/docs/API_REFERENCE.md`](arduino/docs/API_REFERENCE.md)

## Design notes

The driver follows patterns proven in the companion ICM-42688 port:

- **Registers separated** from the driver in `registers.py`. Every address and
  bit field is a `const()`; nothing in the driver is a magic number.
- **Pre-allocated buffers** for hot-path reads, so the GC stays out of the
  realtime measurement loop.
- **Specific exceptions** (`OSError` for bus failures, `RuntimeError` for
  unexpected state); no bare `except:` clauses.
- **Manual register access** rather than `busio` helpers, so the same code
  shape ports cleanly to MicroPython.

## Author : Benkhira Yassine

## License

MIT — see [LICENSE](LICENSE).
