"""Tests for SET/RESET, continuous mode, bandwidth, channels, and selftest."""

import pytest

from mmc5983ma import registers as reg


class TestSetResetCoil:
    def test_set_coil_writes_set_bit(self, mag, i2c):
        i2c.writes.clear()
        mag.set_coil()
        sets = [w for w in i2c.writes if w[1] == reg.REG_INT_CTRL_0 and w[2] & reg.CTRL0_SET]
        assert len(sets) == 1

    def test_reset_coil_writes_reset_bit(self, mag, i2c):
        i2c.writes.clear()
        mag.reset_coil()
        resets = [w for w in i2c.writes if w[1] == reg.REG_INT_CTRL_0 and w[2] & reg.CTRL0_RESET]
        assert len(resets) == 1

    def test_offset_canceled_read_at_zero_field(self, mag, i2c):
        i2c.set_magnetic_raw(reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET)
        x, y, z = mag.offset_canceled_read()
        assert x == pytest.approx(0.0, abs=1e-9)
        assert y == pytest.approx(0.0, abs=1e-9)
        assert z == pytest.approx(0.0, abs=1e-9)

    def test_automatic_set_reset_property(self, mag):
        assert mag.automatic_set_reset is False
        mag.automatic_set_reset = True
        assert mag.automatic_set_reset is True
        mag.automatic_set_reset = False
        assert mag.automatic_set_reset is False


class TestBandwidth:
    @pytest.mark.parametrize("hz,code", [(100, 0x00), (200, 0x01), (400, 0x02), (800, 0x03)])
    def test_bandwidth_setter(self, mag, i2c, hz, code):
        mag.bandwidth = hz
        assert mag.bandwidth == hz
        assert (i2c.regs[reg.REG_INT_CTRL_1] & reg.CTRL1_BW_MASK) == code

    def test_bandwidth_invalid_raises(self, mag):
        with pytest.raises(ValueError, match="bandwidth"):
            mag.bandwidth = 50


class TestContinuousMode:
    def test_configure_continuous_sets_cmm_en(self, mag, i2c):
        mag.configure_continuous_mode(100)
        assert i2c.regs[reg.REG_INT_CTRL_2] & reg.CTRL2_CMM_EN
        assert mag.continuous_mode is True

    def test_continuous_mode_invalid_rate(self, mag):
        with pytest.raises(ValueError, match="rate_hz"):
            mag.configure_continuous_mode(42)

    def test_1000hz_promotes_bandwidth_to_800(self, mag):
        assert mag.bandwidth == 100
        mag.configure_continuous_mode(1000)
        assert mag.bandwidth == 800

    def test_200hz_promotes_bandwidth_from_100(self, mag):
        mag.configure_continuous_mode(200)
        assert mag.bandwidth >= 200

    def test_single_shot_clears_cmm_en(self, mag, i2c):
        mag.configure_continuous_mode(50)
        mag.configure_single_shot_mode()
        assert not (i2c.regs[reg.REG_INT_CTRL_2] & reg.CTRL2_CMM_EN)
        assert mag.continuous_mode is False

    def test_continuous_mode_blocks_until_first_sample_ready(self, mag, i2c):
        # Without the startup wait the driver returned all-zero data on the
        # first read, computing |B|≈1419 µT. The wait must time out cleanly
        # if the chip never asserts MEAS_M_DONE.
        i2c.suppress_meas_done = True
        with pytest.raises(OSError, match="continuous"):
            mag.configure_continuous_mode(100)

    def test_continuous_mode_skips_single_shot_trigger(self, mag, i2c):
        mag.configure_continuous_mode(100)
        i2c.writes.clear()
        _ = mag.magnetic_raw
        triggers = [w for w in i2c.writes if w[1] == reg.REG_INT_CTRL_0 and w[2] & reg.CTRL0_TM_M]
        assert len(triggers) == 0


class TestPeriodicSet:
    @pytest.mark.parametrize("samples,code", [(1, 0), (25, 1), (100, 3), (2000, 7)])
    def test_periodic_set_encodes_count(self, mag, i2c, samples, code):
        mag.configure_periodic_set(samples)
        ctrl2 = i2c.regs[reg.REG_INT_CTRL_2]
        assert ctrl2 & reg.CTRL2_EN_PRD_SET
        assert ((ctrl2 & reg.CTRL2_PRD_SET_MASK) >> reg.CTRL2_PRD_SET_SHIFT) == code

    def test_periodic_set_invalid_count(self, mag):
        with pytest.raises(ValueError, match="sample_count"):
            mag.configure_periodic_set(7)

    def test_disable_periodic_set(self, mag, i2c):
        mag.configure_periodic_set(100)
        mag.disable_periodic_set()
        assert not (i2c.regs[reg.REG_INT_CTRL_2] & reg.CTRL2_EN_PRD_SET)


class TestChannelControl:
    def test_x_disable_then_enable(self, mag, i2c):
        mag.x_enabled = False
        assert i2c.regs[reg.REG_INT_CTRL_1] & reg.CTRL1_X_INHIBIT
        assert mag.x_enabled is False
        mag.x_enabled = True
        assert mag.x_enabled is True

    def test_yz_disable_sets_both_bits(self, mag, i2c):
        mag.yz_enabled = False
        assert (i2c.regs[reg.REG_INT_CTRL_1] & reg.CTRL1_YZ_INHIBIT) == reg.CTRL1_YZ_INHIBIT


class TestInterrupts:
    def test_interrupt_enable(self, mag, i2c):
        mag.interrupt_enabled = True
        assert i2c.regs[reg.REG_INT_CTRL_0] & reg.CTRL0_INT_MEAS_DONE_EN
        assert mag.interrupt_enabled is True

    def test_clear_interrupt_writes_status(self, mag, i2c):
        i2c.writes.clear()
        mag.clear_interrupt()
        status_writes = [w for w in i2c.writes if w[1] == reg.REG_STATUS]
        assert len(status_writes) == 1


class TestSPIMode:
    def test_3wire_spi_setter(self, mag, i2c):
        mag.spi_3wire = True
        assert i2c.regs[reg.REG_INT_CTRL_3] & reg.CTRL3_SPI_3W
        assert mag.spi_3wire is True


class TestSelftest:
    def test_selftest_pass_when_deflection_large(self, mag, i2c):
        # The mock's set_magnetic_raw returns the same payload every read; to
        # simulate a passing selftest we need the ST_ENP and ST_ENM phases to
        # produce different readings. Patch the test helper to vary the
        # injected raw values per call.
        sequence = [
            (reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET),  # baseline
            (reg.ZERO_FIELD_OFFSET + 20000, reg.ZERO_FIELD_OFFSET + 20000, reg.ZERO_FIELD_OFFSET + 20000),  # ST_ENP
            (reg.ZERO_FIELD_OFFSET - 20000, reg.ZERO_FIELD_OFFSET - 20000, reg.ZERO_FIELD_OFFSET - 20000),  # ST_ENM
        ]
        idx = [0]
        original = i2c.writeto_then_readfrom

        def patched(address, out_buf, in_buf):
            if out_buf[0] == reg.REG_X_OUT_0 and len(in_buf) == 7 and idx[0] < len(sequence):
                from tests.conftest import encode_xyz
                payload = encode_xyz(*sequence[idx[0]])
                idx[0] += 1
                for i in range(7):
                    in_buf[i] = payload[i]
                return
            original(address, out_buf, in_buf)

        i2c.writeto_then_readfrom = patched
        assert mag.selftest() is True

    def test_selftest_fail_on_dead_sensor(self, mag, i2c):
        # All three measurements identical => zero deflection => fail.
        i2c.set_magnetic_raw(reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET)
        assert mag.selftest() is False
