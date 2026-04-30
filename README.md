# MMC5983MA


A driver for the [MEMSIC MMC5983MA](https://www.memsic.com/magnetometer-2) 3-axis
magnetometer with 18-bit resolution and ±8 gauss full-scale range.

This repository hosts implementations for several embedded targets:

| Platform      | Status               | Path             |
|---------------|----------------------|------------------|
| CircuitPython | Hardware-validated   | `circuitpython/` |
| MicroPython   | Coming soon          | `micropython/`   |
| Arduino       | Coming soon          | `arduino/`       |

The CircuitPython driver was developed and validated on an
[Adafruit Feather RP2040 with RFM95 LoRa Radio](https://www.adafruit.com/product/5714)
with the sensor on the STEMMA QT bus. Bring-up reads the expected Earth
field (|B| ≈ 47 µT), self-test passes, and a 10-second continuous-mode
stress run shows |B| variation within 0.22 µT.

## Sensor at a glance

- **Resolution:** 18-bit (0.0625 mG / LSB nominal)
- **Range:** ±8 gauss (±800 µT)
- **Output rates:** up to 1000 Hz in continuous mode
- **Interfaces:** I²C (address `0x30`) and 4-wire / 3-wire SPI
- **Notable features:** SET/RESET coils for offset cancellation, on-die
  temperature sensor, programmable bandwidth filter, periodic SET, built-in
  self-test, automatic SET/RESET.

## Quickstart (CircuitPython, I²C)

```python
import board
import busio
from mmc5983ma import MMC5983MA

i2c = busio.I2C(board.SCL, board.SDA)
mag = MMC5983MA(i2c)

x, y, z = mag.magnetic
print(f"X={x:+.2f} µT  Y={y:+.2f} µT  Z={z:+.2f} µT")
```

See [`circuitpython/examples/`](circuitpython/examples/) for SPI, continuous
mode, calibration, self-test, compass-heading, and a comprehensive
[`hardware_bringup.py`](circuitpython/examples/hardware_bringup.py) that
exercises every public path in one run.

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

Per-platform installation steps live in the platform's `docs/INSTALL.md`. For
CircuitPython on a Feather RP2040, see
[`circuitpython/docs/INSTALL.md`](circuitpython/docs/INSTALL.md).

## API reference

See [`circuitpython/docs/API_REFERENCE.md`](circuitpython/docs/API_REFERENCE.md).

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
