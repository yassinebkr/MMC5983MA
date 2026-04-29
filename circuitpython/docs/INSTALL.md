# Installation guide — Adafruit Feather RP2040 with RFM95 LoRa

This guide walks through installing the `mmc5983ma` driver on an
[Adafruit Feather RP2040 RFM95](https://www.adafruit.com/product/5714) running
CircuitPython, the platform this driver was developed and validated on.

The driver is pure CircuitPython with no compiled dependencies, so the same
steps work on any board running CircuitPython 8.x or newer; only the wiring
differs.

## 1. Flash CircuitPython

1. Download the latest CircuitPython UF2 for the
   [Feather RP2040 RFM95](https://circuitpython.org/board/adafruit_feather_rp2040_rfm95/).
2. Plug the Feather into USB while holding `BOOTSEL`. It mounts as
   `RPI-RP2`.
3. Drag the UF2 onto the drive. The board reboots and remounts as
   `CIRCUITPY`.

Verify the version with the REPL (any serial terminal at 115200 baud, or
`screen /dev/cu.usbmodem*` / `tio`):

```python
>>> import sys
>>> sys.version
```

Expect a string starting with `9.x.x` or `8.x.x`.

## 2. Wire the MMC5983MA

The Feather RP2040 RFM95 has a STEMMA QT connector wired to `board.SCL` /
`board.SDA`. Use a STEMMA QT cable to a SparkFun MMC5983MA breakout (or any
3.3 V breakout), or solder direct:

| Sensor pin | Feather pin | Notes                            |
|------------|-------------|----------------------------------|
| `VCC`      | `3.3V`      | Do **not** connect to 5 V.       |
| `GND`      | `GND`       |                                  |
| `SDA`      | `SDA` (GP2) | I²C data                         |
| `SCL`      | `SCL` (GP3) | I²C clock                        |
| `INT`      | optional    | Wire to any free GPIO if used    |

For SPI, share the on-board SPI bus with the RFM95 radio and pick a free CS
pin:

| Sensor pin | Feather pin    | Notes                                   |
|------------|----------------|-----------------------------------------|
| `SCK`      | `SCK` (GP14)   | Shared with RFM95                       |
| `MOSI`     | `MOSI` (GP15)  | Shared with RFM95                       |
| `MISO`     | `MISO` (GP8)   | Shared with RFM95                       |
| `CS`       | `D5` (GP7)     | Any unused digital pin works            |

Confirm the part is on I²C with the REPL:

```python
>>> import board, busio
>>> i2c = busio.I2C(board.SCL, board.SDA)
>>> while not i2c.try_lock(): pass
>>> [hex(a) for a in i2c.scan()]
['0x30']
>>> i2c.unlock()
```

Address `0x30` is the MMC5983MA — there is no strap pin so this address is
fixed.

## 3. Copy the driver to the board

The Feather appears as a USB drive named `CIRCUITPY`. Copy the
`circuitpython/mmc5983ma/` package onto it:

```
CIRCUITPY/
├── code.py
└── lib/
    └── mmc5983ma/
        ├── __init__.py
        └── registers.py
```

`mmc5983ma` lives under `lib/` so CircuitPython can find it. From a shell:

```bash
# macOS / Linux
cp -r circuitpython/mmc5983ma /Volumes/CIRCUITPY/lib/

# Windows (PowerShell)
Copy-Item -Recurse circuitpython\mmc5983ma -Destination "E:\lib\"
```

## 4. Run an example

Copy any file from `circuitpython/examples/` onto `CIRCUITPY` as `code.py`
(this is the auto-run entry point). For a first sanity check use
`basic_i2c.py`:

```bash
cp circuitpython/examples/basic_i2c.py /Volumes/CIRCUITPY/code.py
```

The REPL streams readings:

```
MMC5983MA online
Bandwidth: 100 Hz
Self-test: PASS
X=  +12.34  Y=  -45.67  Z=   +8.91  |B|=  47.82  µT
...
```

A typical magnitude indoors is 25–65 µT (Earth's field plus building
distortion). If you read close to that, the driver is working.

## Troubleshooting

- **`RuntimeError: MMC5983MA not found`** — the I²C scan above reads
  something other than `0x30`. Check power (3.3 V), SDA/SCL, and pull-ups
  (the SparkFun and Adafruit breakouts include them; bare chips do not).
- **`OSError: timed out`** — the chip responded to PROD_ID but never
  asserted `MEAS_M_DONE`. Check that nothing is holding the I²C bus, and
  that the breakout's INT pin is not stuck low driving the bus.
- **Self-test fails** — most often the sensor is in a strong DC field
  (within centimetres of a magnet, motor, or laptop speaker). Move it 30 cm
  clear of ferrous objects and retry. Persistent failure suggests a damaged
  part.
