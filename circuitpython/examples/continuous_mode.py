"""Continuous-mode reads at 100 Hz with automatic SET/RESET.

Demonstrates the throughput regime intended for vehicle attitude estimation:
data ready every 10 ms, no host-driven trigger per sample, periodic SET/RESET
keeping the offset bounded automatically.
"""

import time

import board
import busio

from mmc5983ma import MMC5983MA

SAMPLE_RATE_HZ = 100
RUN_SECONDS = 5


def main():
    i2c = busio.I2C(board.SCL, board.SDA)
    mag = MMC5983MA(i2c)

    mag.bandwidth = 400  # comfortably above the sample rate
    mag.configure_continuous_mode(SAMPLE_RATE_HZ, automatic_set_reset=True)

    print(f"Streaming at {SAMPLE_RATE_HZ} Hz for {RUN_SECONDS} s")
    samples = 0
    t_end = time.monotonic() + RUN_SECONDS
    while time.monotonic() < t_end:
        if mag.status["magnetic_ready"]:
            x, y, z = mag.magnetic
            samples += 1
            if samples % SAMPLE_RATE_HZ == 0:
                print(f"sample {samples}: X={x:+7.2f} Y={y:+7.2f} Z={z:+7.2f} µT")

    mag.configure_single_shot_mode()
    print(f"Captured {samples} samples")


if __name__ == "__main__":
    main()
