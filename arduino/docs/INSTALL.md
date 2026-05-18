# Installation guide — Adafruit Feather RP2040 with RFM95 LoRa

This guide walks through installing the `MMC5983MA` Arduino library on an
[Adafruit Feather RP2040 RFM95](https://www.adafruit.com/product/5714), the
platform this driver was developed and validated on.

The driver is pure C++ and has no compiled dependencies beyond `Wire.h` and
`SPI.h`, so the same steps work on any Arduino-compatible board running an
Arduino core that ships those headers; only the wiring differs.

## 1. Install the Arduino core

The Feather RP2040 RFM95 needs the **Earle Philhower arduino-pico** core
(`rp2040:rp2040:adafruit_feather`).

### With Arduino IDE

1. Open **File → Preferences**.
2. Paste this URL into "Additional Boards Manager URLs":
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
3. Open **Tools → Board → Boards Manager**, search "Raspberry Pi Pico/RP2040
   (Earle Philhower)" and install it.
4. Select **Tools → Board → Raspberry Pi Pico/RP2040 → Adafruit Feather
   RP2040 RFM95**.

### With arduino-cli

```powershell
arduino-cli config init
arduino-cli config add board_manager.additional_urls `
    https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

## 2. Install the library

### Arduino IDE — manual

1. Clone or download this repo.
2. Copy the `arduino/` directory into your Arduino libraries folder and
   rename it to `MMC5983MA`. On Windows that is
   `Documents\Arduino\libraries\MMC5983MA\`.
3. Restart the Arduino IDE. The library now shows up under
   **File → Examples → MMC5983MA**.

### arduino-cli

```powershell
arduino-cli lib install --git-url https://github.com/yassinebkr/MMC5983MA.git
```

The CLI installs the repo as `MMC5983MA` and finds `arduino/library.properties`.

## 3. Wire the MMC5983MA

The Feather RP2040 RFM95 has a STEMMA QT connector wired to the same I2C
bus as `Wire`. For I2C use a STEMMA QT cable to an MMC5983MA breakout (or
solder direct):

### I2C (default)

| Sensor pin | Feather pin | Notes                       |
|------------|-------------|-----------------------------|
| `VCC`      | `3.3V`      | Do **not** connect to 5 V.  |
| `GND`      | `GND`       |                             |
| `SDA`      | `SDA` (GP2) | I2C data                    |
| `SCL`      | `SCL` (GP3) | I2C clock                   |
| `INT`      | optional    | Wire to any free GPIO if used |

To confirm the part responds, run the Arduino IDE's built-in
**File → Examples → Wire → i2c_scanner**. You should see `0x30` reported.
There is no strap pin so this address is fixed.

### SPI (4-wire)

For SPI, share the on-board SPI bus with the RFM95 radio and pick a free
chip-select pin:

| Sensor pin | Feather pin     | Notes                              |
|------------|-----------------|------------------------------------|
| `VCC`      | `3.3V`          |                                    |
| `GND`      | `GND`           |                                    |
| `SCK`      | `SCK` (GP14)    | Shared with RFM95                  |
| `MOSI`     | `MOSI` (GP15)   | Shared with RFM95                  |
| `MISO`     | `MISO` (GP8)    | Shared with RFM95                  |
| `CS`       | `D5` (GP7)      | Any unused digital pin works       |

On most MMC5983MA breakouts the SPI / I2C selection happens via a strap
pad on the board. If you bought a Qwiic / STEMMA QT breakout it is hard-
strapped for I2C; check your breakout's documentation before expecting
SPI to work.

## 4. Run an example

Open **File → Examples → MMC5983MA → BasicI2C**, select your board and
port, and upload. Open the Serial Monitor at **115200 baud**:

```
MMC5983MA online
Bandwidth: 100 Hz
Self-test: PASS
X=  +12.34   Y=  -45.67   Z=   +8.91   |B|=  47.82 uT
...
```

A typical indoor magnitude is 25–65 uT (Earth's field plus building
distortion). If you read close to that, the driver is working.

## Troubleshooting

- **`MMC5983MA not found at 0x30`** — the I2C scan reads something other
  than `0x30`. Check power (3.3 V), SDA/SCL, and pull-ups (Qwiic / STEMMA
  QT breakouts include them; bare chips do not).
- **`read failed` after `MMC5983MA online`** — the chip responded to
  PROD_ID but never asserted `MEAS_M_DONE`. Check that nothing is holding
  the I2C bus, and that the breakout's INT pin is not stuck low driving
  the bus.
- **Self-test fails** — most often the sensor is in a strong DC field
  (within centimetres of a magnet, motor, or laptop speaker). Move it 30
  cm clear of ferrous objects and retry. Persistent failure suggests a
  damaged part.
- **SPI returns garbage** — check the breakout's bus-mode strap, verify
  CS is a unique GPIO (not also driving another peripheral), and confirm
  the SPI pins on the Feather match the table above. The 8 MHz default
  clock is conservative; try lowering it to 1 MHz if you suspect signal
  integrity on jumpers.
