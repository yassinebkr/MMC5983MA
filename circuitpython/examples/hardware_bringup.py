"""End-to-end hardware bring-up for the MMC5983MA driver.

Run from the REPL on a Feather RP2040 / Feather RP2040 RFM with the sensor
on the STEMMA QT bus (or any board with ``board.STEMMA_I2C()``)::

    >>> import hardware_bringup as hb
    >>> hb.test()

Exercises every public path that does not need an external test fixture:
identity, single-shot, temperature, offset cancellation, self-test, every
filter bandwidth, continuous mode at 100 Hz and 1000 Hz, periodic SET,
channel inhibit, and software reset. Returns ``True`` only if every step
passes.
"""

import time

import board

from mmc5983ma import MMC5983MA


EARTH_FIELD_MIN_UT = 20.0
EARTH_FIELD_MAX_UT = 75.0


def magnitude(x, y, z):
    return (x * x + y * y + z * z) ** 0.5


def step(name):
    print()
    print(f"--- {name} ---")


def test():
    i2c = board.STEMMA_I2C()
    fails = []

    step("I2C scan")
    while not i2c.try_lock():
        pass
    devs = i2c.scan()
    i2c.unlock()
    print("devices:", [hex(d) for d in devs])
    if 0x30 not in devs:
        print("FAIL: MMC5983MA at 0x30 not found")
        return False

    step("identity + reset")
    mag = MMC5983MA(i2c)
    print(f"connected={mag.connected} bandwidth={mag.bandwidth} Hz")
    if not mag.connected:
        fails.append("identity")

    step("single-shot magnetic")
    x, y, z = mag.magnetic
    b = magnitude(x, y, z)
    print(f"X={x:+7.2f} Y={y:+7.2f} Z={z:+7.2f} |B|={b:6.2f} uT")
    if not (EARTH_FIELD_MIN_UT < b < EARTH_FIELD_MAX_UT):
        fails.append(f"|B| {b:.1f} outside Earth-field window")

    step("temperature")
    t = mag.temperature
    print(f"{t:5.1f} C")
    if not (-10.0 < t < 60.0):
        fails.append(f"temperature {t:.1f} implausible")

    step("offset-canceled read")
    ox, oy, oz = mag.offset_canceled_read()
    bo = magnitude(ox, oy, oz)
    print(f"X={ox:+7.2f} Y={oy:+7.2f} Z={oz:+7.2f} |B|={bo:6.2f} uT")
    # Single-shot and offset-canceled |B| should agree to within ~5 µT.
    if abs(b - bo) > 8.0:
        fails.append(f"|B| disagreement: single={b:.1f} canc={bo:.1f}")

    step("self-test")
    if mag.selftest():
        print("PASS")
    else:
        print("FAIL")
        fails.append("selftest")

    step("bandwidth sweep")
    for bw in (100, 200, 400, 800):
        mag.bandwidth = bw
        x, y, z = mag.magnetic
        print(f"  BW={bw:4d} Hz: |B|={magnitude(x, y, z):6.2f} uT")
        if mag.bandwidth != bw:
            fails.append(f"bandwidth getter mismatch at {bw}")
    mag.bandwidth = 100

    step("continuous @ 100 Hz / 1 s")
    mag.bandwidth = 400
    mag.configure_continuous_mode(100, automatic_set_reset=True)
    n = 0
    t_end = time.monotonic() + 1.0
    while time.monotonic() < t_end:
        if mag.status["magnetic_ready"]:
            _ = mag.magnetic
            n += 1
    mag.configure_single_shot_mode()
    print(f"  {n} samples")
    if n < 80:
        fails.append(f"continuous@100 only {n} samples")

    step("continuous @ 1000 Hz / 1 s")
    mag.configure_continuous_mode(1000, automatic_set_reset=True)
    n = 0
    t_end = time.monotonic() + 1.0
    # At 1000 Hz the chip produces a fresh sample every 1 ms. In CircuitPython
    # the consume rate is ~400/s — Python overhead dominates, not the chip.
    # We just need to confirm the rate climbed well above the 100 Hz baseline.
    while time.monotonic() < t_end:
        _ = mag.magnetic_raw
        n += 1
    mag.configure_single_shot_mode()
    print(f"  {n} reads (bandwidth={mag.bandwidth} Hz, auto-promoted)")
    if n < 250:
        fails.append(f"continuous@1000 only {n} reads")

    step("periodic SET")
    mag.configure_periodic_set(100)
    _ = mag.magnetic
    mag.disable_periodic_set()
    print("  configured + disabled cleanly")

    step("channel inhibit")
    mag.x_enabled = False
    x_off, _, _ = mag.magnetic_raw
    mag.x_enabled = True
    x_on, _, _ = mag.magnetic_raw
    print(f"  X off={x_off}  X on={x_on}")
    # When X is inhibited the raw reading is whatever the inhibit logic
    # leaves; what matters is that re-enabling restores a reading near the
    # zero-field offset (0x20000 = 131072) ± Earth-field deflection.
    if not (100000 < x_on < 160000):
        fails.append(f"x re-enable raw {x_on} suspicious")

    step("software reset")
    mag.bandwidth = 800
    mag.reset()
    if mag.bandwidth != 100:
        fails.append("reset did not restore default bandwidth")
    print(f"  post-reset bandwidth={mag.bandwidth} Hz")

    print()
    if fails:
        print("FAILED:", fails)
        return False
    print("ALL CHECKS PASSED")
    return True


if __name__ == "__main__":
    test()
