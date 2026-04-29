# MMC5983MA

A driver for the [MEMSIC MMC5983MA](https://www.memsic.com/magnetometer-2) 3-axis
magnetometer with 18-bit resolution and ±8 gauss full-scale range.

This repository hosts implementations for several embedded targets:

| Platform      | Status        | Path             |
|---------------|---------------|------------------|
| CircuitPython | Available     | `circuitpython/` |
| MicroPython   | Coming soon   | `micropython/`   |
| Arduino       | Coming soon   | `arduino/`       |

The CircuitPython driver was developed and validated on an
[Adafruit Feather RP2040 with RFM95 LoRa Radio](https://www.adafruit.com/product/5714),
the target platform for the aerospace telemetry application that drives this
project.

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
mode, calibration, self-test, and compass-heading demos.

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

## License

MIT — see [LICENSE](LICENSE).
