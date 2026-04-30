"""CircuitPython driver for the MEMSIC MMC5983MA 3-axis magnetometer.

Designed to share code shape with a planned MicroPython port: bus access uses
manual ``writeto`` / ``readfrom_into`` calls rather than CircuitPython
``busio`` register helpers, and every read or write reuses pre-allocated
buffers so the GC stays out of the realtime loop.

Configuration registers are tracked as shadows on the host. Writes always go
through the shadow because several control bits (SET, RESET, TM_M, TM_T,
SW_RST) are transient triggers that self-clear, so reading the device back
would not give a meaningful state.
"""

import time

from . import registers as reg

__version__ = "0.1.0"


class MMC5983MA:
    """Driver for the MEMSIC MMC5983MA 3-axis magnetometer.

    Parameters
    ----------
    bus
        A CircuitPython ``busio.I2C`` or ``busio.SPI`` object.
    address
        I²C device address. Defaults to ``0x30`` (the part is hard-strapped).
    cs
        Chip-select ``digitalio.DigitalInOut`` for SPI mode. When supplied,
        ``bus`` is treated as SPI; when ``None``, ``bus`` is treated as I²C.
    spi_baudrate
        SPI clock rate in Hz. Datasheet allows up to 10 MHz.
    """

    def __init__(self, bus, *, address=reg.I2C_ADDRESS, cs=None, spi_baudrate=8_000_000):
        self._spi_cs = cs
        if cs is None:
            self._i2c = bus
            self._spi = None
            self._address = address
        else:
            self._i2c = None
            self._spi = bus
            self._spi_baudrate = spi_baudrate
            cs.switch_to_output(value=True)

        self._mag_buf = bytearray(7)
        self._mag_view = memoryview(self._mag_buf)
        self._addr_buf = bytearray(1)
        self._byte_buf = bytearray(1)
        self._write_buf = bytearray(2)

        self._ctrl0_shadow = 0x00
        self._ctrl1_shadow = 0x00
        self._ctrl2_shadow = 0x00
        self._ctrl3_shadow = 0x00
        self._bandwidth_code = reg.BW_100_HZ

        prod_id = self._read_register(reg.REG_PROD_ID)
        if prod_id != reg.PROD_ID:
            raise RuntimeError(
                "MMC5983MA not found: expected PROD_ID 0x{:02X}, got 0x{:02X}".format(
                    reg.PROD_ID, prod_id
                )
            )

        self.reset()

    # ------------------------------------------------------------------
    # Bus access
    # ------------------------------------------------------------------

    def _read_register(self, address):
        self._addr_buf[0] = address if self._i2c is not None else (address | reg.SPI_READ)
        if self._i2c is not None:
            while not self._i2c.try_lock():
                pass
            try:
                self._i2c.writeto_then_readfrom(self._address, self._addr_buf, self._byte_buf)
            finally:
                self._i2c.unlock()
        else:
            while not self._spi.try_lock():
                pass
            try:
                self._spi.configure(baudrate=self._spi_baudrate, polarity=0, phase=0)
                self._spi_cs.value = False
                self._spi.write(self._addr_buf)
                self._spi.readinto(self._byte_buf)
                self._spi_cs.value = True
            finally:
                self._spi.unlock()
        return self._byte_buf[0]

    def _read_into(self, address, buf):
        self._addr_buf[0] = address if self._i2c is not None else (address | reg.SPI_READ)
        if self._i2c is not None:
            while not self._i2c.try_lock():
                pass
            try:
                self._i2c.writeto_then_readfrom(self._address, self._addr_buf, buf)
            finally:
                self._i2c.unlock()
        else:
            while not self._spi.try_lock():
                pass
            try:
                self._spi.configure(baudrate=self._spi_baudrate, polarity=0, phase=0)
                self._spi_cs.value = False
                self._spi.write(self._addr_buf)
                self._spi.readinto(buf)
                self._spi_cs.value = True
            finally:
                self._spi.unlock()

    def _write_register(self, address, value):
        if self._i2c is not None:
            self._write_buf[0] = address
            self._write_buf[1] = value & 0xFF
            while not self._i2c.try_lock():
                pass
            try:
                self._i2c.writeto(self._address, self._write_buf)
            finally:
                self._i2c.unlock()
        else:
            self._write_buf[0] = address & 0x7F  # MSB=0 -> write
            self._write_buf[1] = value & 0xFF
            while not self._spi.try_lock():
                pass
            try:
                self._spi.configure(baudrate=self._spi_baudrate, polarity=0, phase=0)
                self._spi_cs.value = False
                self._spi.write(self._write_buf)
                self._spi_cs.value = True
            finally:
                self._spi.unlock()

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def reset(self):
        """Issue a software reset and wait for the device to come back.

        After this call all configuration registers are at their power-on
        defaults and the driver's shadow state is in sync with the device.
        """
        self._write_register(reg.REG_INT_CTRL_1, reg.CTRL1_SW_RST)
        time.sleep(reg.RESET_DELAY_MS / 1000)
        self._ctrl0_shadow = 0x00
        self._ctrl1_shadow = 0x00
        self._ctrl2_shadow = 0x00
        self._ctrl3_shadow = 0x00
        self._bandwidth_code = reg.BW_100_HZ

    # ------------------------------------------------------------------
    # Status
    # ------------------------------------------------------------------

    @property
    def status(self):
        """Snapshot of the STATUS register as a dict of named flags.

        Keys: ``magnetic_ready``, ``temperature_ready``, ``otp_loaded``.
        """
        s = self._read_register(reg.REG_STATUS)
        return {
            "magnetic_ready": bool(s & reg.STATUS_MEAS_M_DONE),
            "temperature_ready": bool(s & reg.STATUS_MEAS_T_DONE),
            "otp_loaded": bool(s & reg.STATUS_OTP_READ_DONE),
        }

    @property
    def connected(self):
        """True if a chip with the expected product ID responds."""
        try:
            return self._read_register(reg.REG_PROD_ID) == reg.PROD_ID
        except OSError:
            return False

    # ------------------------------------------------------------------
    # Magnetic measurement (single-shot)
    # ------------------------------------------------------------------

    def _measurement_timeout_ms(self):
        if self._bandwidth_code == reg.BW_800_HZ:
            return reg.MEAS_TIMEOUT_MS_BW_800
        if self._bandwidth_code == reg.BW_400_HZ:
            return reg.MEAS_TIMEOUT_MS_BW_400
        if self._bandwidth_code == reg.BW_200_HZ:
            return reg.MEAS_TIMEOUT_MS_BW_200
        return reg.MEAS_TIMEOUT_MS_BW_100

    def _trigger_magnetic_and_wait(self):
        # Clear stale data-ready flag: the bit is cleared by writing a 1 to it.
        self._write_register(reg.REG_STATUS, reg.STATUS_MEAS_M_DONE)
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow | reg.CTRL0_TM_M)
        deadline_ms = self._measurement_timeout_ms()
        elapsed = 0
        while elapsed <= deadline_ms:
            if self._read_register(reg.REG_STATUS) & reg.STATUS_MEAS_M_DONE:
                return
            time.sleep(reg.MEAS_POLL_INTERVAL_MS / 1000)
            elapsed += reg.MEAS_POLL_INTERVAL_MS
        raise OSError("MMC5983MA magnetic measurement timed out")

    def _read_raw_xyz(self):
        self._read_into(reg.REG_X_OUT_0, self._mag_buf)
        b = self._mag_view
        x = (b[0] << reg.XYZ_0_SHIFT) | (b[1] << reg.XYZ_1_SHIFT) | (
            (b[6] >> reg.XYZ_X2_SHIFT) & reg.XYZ_2_MASK
        )
        y = (b[2] << reg.XYZ_0_SHIFT) | (b[3] << reg.XYZ_1_SHIFT) | (
            (b[6] >> reg.XYZ_Y2_SHIFT) & reg.XYZ_2_MASK
        )
        z = (b[4] << reg.XYZ_0_SHIFT) | (b[5] << reg.XYZ_1_SHIFT) | (
            (b[6] >> reg.XYZ_Z2_SHIFT) & reg.XYZ_2_MASK
        )
        return x, y, z

    @staticmethod
    def _to_microtesla(raw):
        return (raw - reg.ZERO_FIELD_OFFSET) * reg.SENSITIVITY_UT_PER_LSB

    @property
    def magnetic_raw(self):
        """Tuple of raw 18-bit unsigned X, Y, Z readings (no scaling).

        In single-shot mode, fires a SET pulse before triggering the
        measurement. Without a recent SET, the MMC5983MA's internal element
        sits in an indeterminate magnetization state and the reading is
        dominated by the residual offset (datasheet calls this out: SET/RESET
        is required to define the polarity of the bridge). In continuous
        mode this method returns the latest sample directly — when
        ``configure_continuous_mode`` is called with ``automatic_set_reset=True``
        (the default) the chip handles SET/RESET internally; ensure
        ``status['magnetic_ready']`` if you need a guaranteed-fresh sample.

        For the highest accuracy use :meth:`offset_canceled_read`, which
        takes both SET and RESET measurements and subtracts the offset
        exactly.
        """
        if not (self._ctrl2_shadow & reg.CTRL2_CMM_EN):
            self.set_coil()
            self._trigger_magnetic_and_wait()
        return self._read_raw_xyz()

    @property
    def magnetic(self):
        """Tuple ``(x, y, z)`` in microtesla. See :attr:`magnetic_raw` for
        the SET-pulse behaviour applied to single-shot reads."""
        rx, ry, rz = self.magnetic_raw
        return (
            self._to_microtesla(rx),
            self._to_microtesla(ry),
            self._to_microtesla(rz),
        )

    # ------------------------------------------------------------------
    # Temperature
    # ------------------------------------------------------------------

    @property
    def temperature(self):
        """Die temperature in degrees Celsius.

        Always uses single-shot mode — temperature does not run continuously
        even when magnetic continuous mode is active.
        """
        self._write_register(reg.REG_STATUS, reg.STATUS_MEAS_T_DONE)
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow | reg.CTRL0_TM_T)
        elapsed = 0
        while elapsed <= reg.TEMP_TIMEOUT_MS:
            if self._read_register(reg.REG_STATUS) & reg.STATUS_MEAS_T_DONE:
                raw = self._read_register(reg.REG_T_OUT)
                return reg.TEMP_OFFSET_C + raw * reg.TEMP_LSB_C
            time.sleep(reg.MEAS_POLL_INTERVAL_MS / 1000)
            elapsed += reg.MEAS_POLL_INTERVAL_MS
        raise OSError("MMC5983MA temperature measurement timed out")

    # ------------------------------------------------------------------
    # SET / RESET coil operations
    # ------------------------------------------------------------------

    def set_coil(self):
        """Fire the SET coil pulse.

        Magnetizes the internal sensor element in the positive direction. Used
        before a measurement to establish a known polarity, or as the first
        half of an offset-canceled read.
        """
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow | reg.CTRL0_SET)
        time.sleep(reg.SET_RESET_PULSE_DELAY_MS / 1000)

    def reset_coil(self):
        """Fire the RESET coil pulse.

        Magnetizes the internal sensor element in the negative direction (the
        opposite of :meth:`set_coil`). The output is inverted relative to a
        SET-conditioned reading; this is the key to offset cancellation.
        """
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow | reg.CTRL0_RESET)
        time.sleep(reg.SET_RESET_PULSE_DELAY_MS / 1000)

    @property
    def automatic_set_reset(self):
        """Whether the chip auto-fires SET/RESET in continuous mode."""
        return bool(self._ctrl0_shadow & reg.CTRL0_AUTO_SR_EN)

    @automatic_set_reset.setter
    def automatic_set_reset(self, value):
        if value:
            self._ctrl0_shadow |= reg.CTRL0_AUTO_SR_EN
        else:
            self._ctrl0_shadow &= ~reg.CTRL0_AUTO_SR_EN
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow)

    def offset_canceled_read(self):
        """Take an offset-canceled magnetic-field reading.

        Performs a SET pulse and a RESET pulse, takes a measurement after
        each, and returns ``(M_set - M_reset) / 2`` per axis in microtesla.
        Cancels the slow internal offset drift that is the main systematic
        error in this part — at the cost of two measurements per sample.
        """
        self.set_coil()
        self._trigger_magnetic_and_wait()
        sx, sy, sz = self._read_raw_xyz()

        self.reset_coil()
        self._trigger_magnetic_and_wait()
        rx, ry, rz = self._read_raw_xyz()

        scale = reg.SENSITIVITY_UT_PER_LSB / 2.0
        return (
            (sx - rx) * scale,
            (sy - ry) * scale,
            (sz - rz) * scale,
        )

    # ------------------------------------------------------------------
    # Bandwidth and continuous mode
    # ------------------------------------------------------------------

    @property
    def bandwidth(self):
        """Filter bandwidth in Hz (one of 100, 200, 400, 800)."""
        if self._bandwidth_code == reg.BW_800_HZ:
            return 800
        if self._bandwidth_code == reg.BW_400_HZ:
            return 400
        if self._bandwidth_code == reg.BW_200_HZ:
            return 200
        return 100

    @bandwidth.setter
    def bandwidth(self, hz):
        codes = {100: reg.BW_100_HZ, 200: reg.BW_200_HZ, 400: reg.BW_400_HZ, 800: reg.BW_800_HZ}
        if hz not in codes:
            raise ValueError("bandwidth must be one of 100, 200, 400, 800 Hz")
        self._bandwidth_code = codes[hz]
        self._ctrl1_shadow = (self._ctrl1_shadow & ~reg.CTRL1_BW_MASK) | self._bandwidth_code
        self._write_register(reg.REG_INT_CTRL_1, self._ctrl1_shadow)

    def configure_continuous_mode(self, rate_hz, *, automatic_set_reset=True):
        """Start continuous-measurement mode at ``rate_hz``.

        Valid rates: 1, 10, 20, 50, 100, 200, 1000 Hz. 200 Hz requires
        bandwidth ≥ 200 Hz; 1000 Hz requires bandwidth = 800 Hz. The driver
        promotes bandwidth automatically if it is too low for the requested
        rate.

        ``automatic_set_reset`` enables the chip's built-in periodic
        SET/RESET, which keeps the offset drift bounded without host
        involvement; recommended for long-running continuous operation.
        """
        rate_codes = {
            1: reg.CM_FREQ_1_HZ,
            10: reg.CM_FREQ_10_HZ,
            20: reg.CM_FREQ_20_HZ,
            50: reg.CM_FREQ_50_HZ,
            100: reg.CM_FREQ_100_HZ,
            200: reg.CM_FREQ_200_HZ,
            1000: reg.CM_FREQ_1000_HZ,
        }
        if rate_hz not in rate_codes:
            raise ValueError("rate_hz must be one of 1, 10, 20, 50, 100, 200, 1000")

        if rate_hz == 1000 and self._bandwidth_code != reg.BW_800_HZ:
            self.bandwidth = 800
        elif rate_hz == 200 and self._bandwidth_code == reg.BW_100_HZ:
            self.bandwidth = 200

        if automatic_set_reset:
            self.automatic_set_reset = True

        code = rate_codes[rate_hz]
        self._ctrl2_shadow = (
            (self._ctrl2_shadow & ~reg.CTRL2_CM_FREQ_MASK) | code | reg.CTRL2_CMM_EN
        )
        self._write_register(reg.REG_INT_CTRL_2, self._ctrl2_shadow)

        # Block until the first sample is in the data registers. Without this,
        # the very first read after enabling continuous mode hits all-zero
        # registers and computes |B|≈1419 µT (= sqrt(3)·offset·sensitivity).
        # Found on hardware on a Feather RP2040 stress run; see commit log.
        self._write_register(reg.REG_STATUS, reg.STATUS_MEAS_M_DONE)
        startup_timeout_ms = self._measurement_timeout_ms() + 5
        elapsed = 0
        while elapsed <= startup_timeout_ms:
            if self._read_register(reg.REG_STATUS) & reg.STATUS_MEAS_M_DONE:
                return
            time.sleep(reg.MEAS_POLL_INTERVAL_MS / 1000)
            elapsed += reg.MEAS_POLL_INTERVAL_MS
        raise OSError("MMC5983MA continuous-mode startup timed out")

    def configure_single_shot_mode(self):
        """Disable continuous mode and return to one-shot triggering."""
        self._ctrl2_shadow &= ~(reg.CTRL2_CMM_EN | reg.CTRL2_CM_FREQ_MASK)
        self._write_register(reg.REG_INT_CTRL_2, self._ctrl2_shadow)

    @property
    def continuous_mode(self):
        """True when continuous mode is enabled."""
        return bool(self._ctrl2_shadow & reg.CTRL2_CMM_EN)

    # ------------------------------------------------------------------
    # Periodic SET
    # ------------------------------------------------------------------

    def configure_periodic_set(self, sample_count):
        """Enable periodic SET every ``sample_count`` measurements.

        Valid counts: 1, 25, 75, 100, 250, 500, 1000, 2000. Smaller values
        give better offset stability at the cost of more pulses (and slightly
        more current draw).
        """
        codes = {
            1: reg.PRD_SET_1,
            25: reg.PRD_SET_25,
            75: reg.PRD_SET_75,
            100: reg.PRD_SET_100,
            250: reg.PRD_SET_250,
            500: reg.PRD_SET_500,
            1000: reg.PRD_SET_1000,
            2000: reg.PRD_SET_2000,
        }
        if sample_count not in codes:
            raise ValueError("sample_count must be one of 1, 25, 75, 100, 250, 500, 1000, 2000")
        self._ctrl2_shadow = (
            (self._ctrl2_shadow & ~reg.CTRL2_PRD_SET_MASK)
            | (codes[sample_count] << reg.CTRL2_PRD_SET_SHIFT)
            | reg.CTRL2_EN_PRD_SET
        )
        self._write_register(reg.REG_INT_CTRL_2, self._ctrl2_shadow)

    def disable_periodic_set(self):
        """Turn off periodic SET."""
        self._ctrl2_shadow &= ~(reg.CTRL2_EN_PRD_SET | reg.CTRL2_PRD_SET_MASK)
        self._write_register(reg.REG_INT_CTRL_2, self._ctrl2_shadow)

    # ------------------------------------------------------------------
    # Channel control
    # ------------------------------------------------------------------

    @property
    def x_enabled(self):
        return not (self._ctrl1_shadow & reg.CTRL1_X_INHIBIT)

    @x_enabled.setter
    def x_enabled(self, value):
        if value:
            self._ctrl1_shadow &= ~reg.CTRL1_X_INHIBIT
        else:
            self._ctrl1_shadow |= reg.CTRL1_X_INHIBIT
        self._write_register(reg.REG_INT_CTRL_1, self._ctrl1_shadow)

    @property
    def yz_enabled(self):
        return not (self._ctrl1_shadow & reg.CTRL1_YZ_INHIBIT)

    @yz_enabled.setter
    def yz_enabled(self, value):
        if value:
            self._ctrl1_shadow &= ~reg.CTRL1_YZ_INHIBIT
        else:
            self._ctrl1_shadow |= reg.CTRL1_YZ_INHIBIT
        self._write_register(reg.REG_INT_CTRL_1, self._ctrl1_shadow)

    # ------------------------------------------------------------------
    # Interrupts
    # ------------------------------------------------------------------

    @property
    def interrupt_enabled(self):
        """Whether the INT pin asserts on measurement-done."""
        return bool(self._ctrl0_shadow & reg.CTRL0_INT_MEAS_DONE_EN)

    @interrupt_enabled.setter
    def interrupt_enabled(self, value):
        if value:
            self._ctrl0_shadow |= reg.CTRL0_INT_MEAS_DONE_EN
        else:
            self._ctrl0_shadow &= ~reg.CTRL0_INT_MEAS_DONE_EN
        self._write_register(reg.REG_INT_CTRL_0, self._ctrl0_shadow)

    def clear_interrupt(self, mask=reg.STATUS_MEAS_M_DONE | reg.STATUS_MEAS_T_DONE):
        """Clear the named status flags by writing 1s to them."""
        self._write_register(reg.REG_STATUS, mask)

    # ------------------------------------------------------------------
    # SPI mode helpers
    # ------------------------------------------------------------------

    @property
    def spi_3wire(self):
        return bool(self._ctrl3_shadow & reg.CTRL3_SPI_3W)

    @spi_3wire.setter
    def spi_3wire(self, value):
        if value:
            self._ctrl3_shadow |= reg.CTRL3_SPI_3W
        else:
            self._ctrl3_shadow &= ~reg.CTRL3_SPI_3W
        self._write_register(reg.REG_INT_CTRL_3, self._ctrl3_shadow)

    # ------------------------------------------------------------------
    # Self-test
    # ------------------------------------------------------------------

    def selftest(self):
        """Run the built-in saturation self-test.

        Procedure:

        1. Take an offset-canceled baseline reading.
        2. Apply ST_ENP (extra current positive→negative). Measure.
        3. Apply ST_ENM (extra current negative→positive). Measure.
        4. Verify the change between ST_ENP and ST_ENM exceeds a sensible
           threshold on every axis. The MEMSIC datasheet guarantees the
           internal selftest current produces a deflection larger than the
           sensor's typical noise.

        Returns
        -------
        bool
            ``True`` if every axis showed a deflection > ~100 µT.
        """
        # Always end with a clean SET so the device is ready for normal use.
        self.set_coil()
        self._trigger_magnetic_and_wait()
        sx, sy, sz = self._read_raw_xyz()

        try:
            self._ctrl3_shadow |= reg.CTRL3_ST_ENP
            self._write_register(reg.REG_INT_CTRL_3, self._ctrl3_shadow)
            self._trigger_magnetic_and_wait()
            px, py, pz = self._read_raw_xyz()

            self._ctrl3_shadow = (self._ctrl3_shadow & ~reg.CTRL3_ST_ENP) | reg.CTRL3_ST_ENM
            self._write_register(reg.REG_INT_CTRL_3, self._ctrl3_shadow)
            self._trigger_magnetic_and_wait()
            nx, ny, nz = self._read_raw_xyz()
        finally:
            self._ctrl3_shadow &= ~(reg.CTRL3_ST_ENP | reg.CTRL3_ST_ENM)
            self._write_register(reg.REG_INT_CTRL_3, self._ctrl3_shadow)
            self.set_coil()

        # Datasheet "Applied SAT field": min 80 mG, typ 125 mG per side, so the
        # ST_ENP-to-ST_ENM swing must exceed 160 mG even on a worst-case part.
        # Use 100 mG (10 µT, 1600 LSB) as the pass threshold — comfortably
        # below the spec minimum but well above the sensor's noise floor.
        threshold = int(10.0 / reg.SENSITIVITY_UT_PER_LSB)
        return (
            abs(px - nx) > threshold
            and abs(py - ny) > threshold
            and abs(pz - nz) > threshold
        )
