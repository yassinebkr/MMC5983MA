"""10-second continuous stability test. Verify no exceptions, |B| stays
in the Earth-field window, and sample rate is steady.

Copy to the CIRCUITPY root and run from the REPL:

    >>> import stress as s
    >>> s.run()
"""

import time

import board

from mmc5983ma import MMC5983MA


def run(duration_s=10.0):
    i2c = board.STEMMA_I2C()
    mag = MMC5983MA(i2c)
    mag.bandwidth = 400
    mag.configure_continuous_mode(100, automatic_set_reset=True)

    n = 0
    bmin = 1000.0
    bmax = 0.0
    bsum = 0.0
    t0 = time.monotonic()
    t_end = t0 + duration_s
    last_print = t0
    try:
        while time.monotonic() < t_end:
            x, y, z = mag.magnetic
            b = (x * x + y * y + z * z) ** 0.5
            n += 1
            bsum += b
            if b < bmin:
                bmin = b
            if b > bmax:
                bmax = b
            now = time.monotonic()
            if now - last_print >= 2.0:
                print(f"t={now-t0:4.1f}s n={n} |B| min={bmin:.2f} max={bmax:.2f} avg={bsum/n:.2f}")
                last_print = now
    finally:
        mag.configure_single_shot_mode()

    elapsed = time.monotonic() - t0
    print()
    print(f"final: {n} samples in {elapsed:.2f}s ({n/elapsed:.1f}/s)")
    print(f"|B|: min={bmin:.2f} max={bmax:.2f} avg={bsum/n:.2f} span={bmax-bmin:.2f} uT")
    if bmin < 20.0 or bmax > 80.0:
        print("WARN: |B| stepped outside Earth-field window")
        return False
    if bmax - bmin > 10.0:
        print("WARN: |B| span unusually large for a stationary sensor")
        return False
    print("STABLE")
    return True


if __name__ == "__main__":
    run()
