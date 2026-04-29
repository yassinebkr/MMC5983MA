"""Run the built-in self-test and print a verdict.

The MMC5983MA's on-die self-test injects a known current to deflect the
sensor by 80–175 mG per side. The driver compares deflections under positive
and negative selftest current and reports a pass when every axis exceeds the
spec floor.
"""

import board
import busio

from mmc5983ma import MMC5983MA


def main():
    i2c = busio.I2C(board.SCL, board.SDA)
    mag = MMC5983MA(i2c)

    print(f"Connected: {mag.connected}")
    print(f"Bandwidth: {mag.bandwidth} Hz")

    if mag.selftest():
        print("SELFTEST: PASS")
    else:
        print("SELFTEST: FAIL — sensor may be damaged or in a strong DC field")


if __name__ == "__main__":
    main()
