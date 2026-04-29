"""Basic single-shot magnetic-field reading over SPI.

Tested on Adafruit Feather RP2040 with RFM95 LoRa Radio. The MMC5983MA shares
``board.SPI`` with the on-board RFM95 radio; only the chip-select line is
unique to the magnetometer. Wire the sensor's CS to ``board.D5`` (any free
digital pin works — update ``CS_PIN`` to match).
"""

import time

import board
import busio
import digitalio

from mmc5983ma import MMC5983MA

CS_PIN = board.D5


def main():
    spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)
    cs = digitalio.DigitalInOut(CS_PIN)
    mag = MMC5983MA(spi, cs=cs)

    print("MMC5983MA online (SPI)")
    print(f"Self-test: {'PASS' if mag.selftest() else 'FAIL'}")

    while True:
        x, y, z = mag.magnetic
        print(f"X={x:+8.2f}  Y={y:+8.2f}  Z={z:+8.2f}  µT")
        time.sleep(0.1)


if __name__ == "__main__":
    main()
