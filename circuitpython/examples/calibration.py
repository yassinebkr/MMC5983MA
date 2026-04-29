"""Hard-iron offset calibration.

Rotate the sensor through every orientation while this script runs. The
script collects min/max field on each axis and computes the hard-iron offset
that should be subtracted from raw readings so the resulting field is centred
on zero.

This addresses the *static* offset from nearby ferrous material on the
vehicle (battery clips, screws). It does not address soft-iron distortion —
that requires an ellipsoid fit, which is heavier than what fits in a
demo example.
"""

import time

import board
import busio

from mmc5983ma import MMC5983MA

CALIBRATION_SECONDS = 20


def main():
    i2c = busio.I2C(board.SCL, board.SDA)
    mag = MMC5983MA(i2c)
    mag.configure_continuous_mode(100, automatic_set_reset=True)

    print(f"Rotate the sensor through every orientation for {CALIBRATION_SECONDS} s")
    print("Start now...")

    x_min = y_min = z_min = float("inf")
    x_max = y_max = z_max = float("-inf")
    t_end = time.monotonic() + CALIBRATION_SECONDS
    while time.monotonic() < t_end:
        if mag.status["magnetic_ready"]:
            x, y, z = mag.magnetic
            if x < x_min:
                x_min = x
            if x > x_max:
                x_max = x
            if y < y_min:
                y_min = y
            if y > y_max:
                y_max = y
            if z < z_min:
                z_min = z
            if z > z_max:
                z_max = z

    mag.configure_single_shot_mode()

    x_offset = (x_max + x_min) / 2
    y_offset = (y_max + y_min) / 2
    z_offset = (z_max + z_min) / 2

    print()
    print("Hard-iron offsets (subtract from raw readings):")
    print(f"  X: {x_offset:+8.2f} µT")
    print(f"  Y: {y_offset:+8.2f} µT")
    print(f"  Z: {z_offset:+8.2f} µT")
    print()
    print("Range per axis (peak to peak):")
    print(f"  X: {x_max - x_min:7.2f} µT")
    print(f"  Y: {y_max - y_min:7.2f} µT")
    print(f"  Z: {z_max - z_min:7.2f} µT")


if __name__ == "__main__":
    main()
