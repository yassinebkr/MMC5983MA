"""Basic single-shot magnetic-field reading over I²C.

Tested on Adafruit Feather RP2040 with RFM95 LoRa Radio. The MMC5983MA is
wired to the STEMMA QT connector (or to ``board.SCL`` / ``board.SDA``) and
runs from 3.3 V.
"""

import time

import board
import busio

from mmc5983ma import MMC5983MA


def main():
    i2c = busio.I2C(board.SCL, board.SDA)
    mag = MMC5983MA(i2c)

    print("MMC5983MA online")
    print(f"Bandwidth: {mag.bandwidth} Hz")
    print(f"Self-test: {'PASS' if mag.selftest() else 'FAIL'}")

    while True:
        x, y, z = mag.magnetic
        magnitude = (x * x + y * y + z * z) ** 0.5
        print(f"X={x:+8.2f}  Y={y:+8.2f}  Z={z:+8.2f}  |B|={magnitude:7.2f}  µT")
        time.sleep(0.1)


if __name__ == "__main__":
    main()
