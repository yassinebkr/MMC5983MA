"""Compute and display compass heading.

Computes the heading angle from the X/Y components of the magnetic field.
Assumes the sensor is held roughly level — full 3D heading needs an IMU and
tilt compensation, which is beyond a single-sensor example.

For best accuracy, run ``calibration.py`` first and substitute the hard-iron
offsets below.
"""

import math
import time

import board
import busio

from mmc5983ma import MMC5983MA

# Substitute values from calibration.py for your environment.
X_OFFSET_UT = 0.0
Y_OFFSET_UT = 0.0


def heading_degrees(x, y):
    angle = math.degrees(math.atan2(y, x))
    return angle + 360.0 if angle < 0 else angle


def main():
    i2c = busio.I2C(board.SCL, board.SDA)
    mag = MMC5983MA(i2c)
    mag.configure_continuous_mode(50, automatic_set_reset=True)

    print("Compass heading (degrees, 0 = north when X-axis points north):")
    while True:
        if mag.status["magnetic_ready"]:
            x, y, _ = mag.magnetic
            heading = heading_degrees(x - X_OFFSET_UT, y - Y_OFFSET_UT)
            print(f"heading={heading:6.1f}°")
        time.sleep(0.1)


if __name__ == "__main__":
    main()
