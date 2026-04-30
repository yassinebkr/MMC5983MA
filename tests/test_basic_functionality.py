"""Basic-functionality tests: init, reset, magnetic, temperature, status."""

import pytest

from mmc5983ma import MMC5983MA
from mmc5983ma import registers as reg


class TestInitialization:
    def test_init_succeeds_with_correct_prod_id(self, mag, i2c):
        assert i2c.regs[reg.REG_PROD_ID] == reg.PROD_ID

    def test_init_raises_on_wrong_prod_id(self, i2c, monkeypatch):
        monkeypatch.setattr("time.sleep", lambda _s: None)
        i2c.regs[reg.REG_PROD_ID] = 0x42
        with pytest.raises(RuntimeError, match="PROD_ID"):
            MMC5983MA(i2c)

    def test_init_issues_software_reset(self, mag, i2c):
        reset_writes = [w for w in i2c.writes if w[1] == reg.REG_INT_CTRL_1 and w[2] & reg.CTRL1_SW_RST]
        assert len(reset_writes) >= 1

    def test_default_bandwidth_is_100_hz(self, mag):
        assert mag.bandwidth == 100


class TestStatus:
    def test_connected_returns_true(self, mag):
        assert mag.connected is True

    def test_status_returns_named_flags(self, mag, i2c):
        i2c.regs[reg.REG_STATUS] = reg.STATUS_MEAS_M_DONE | reg.STATUS_OTP_READ_DONE
        s = mag.status
        assert s["magnetic_ready"] is True
        assert s["otp_loaded"] is True
        assert s["temperature_ready"] is False


class TestMagneticReading:
    def test_zero_field_reads_as_zero(self, mag, i2c):
        i2c.set_magnetic_raw(reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET)
        x, y, z = mag.magnetic
        assert x == pytest.approx(0.0, abs=1e-9)
        assert y == pytest.approx(0.0, abs=1e-9)
        assert z == pytest.approx(0.0, abs=1e-9)

    def test_known_field_scales_to_microtesla(self, mag, i2c):
        # 16000 LSB above zero = 16000 * 0.00625 = 100 µT
        i2c.set_magnetic_raw(reg.ZERO_FIELD_OFFSET + 16000, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET)
        x, y, z = mag.magnetic
        assert x == pytest.approx(100.0, abs=0.01)
        assert y == pytest.approx(0.0, abs=1e-9)
        assert z == pytest.approx(0.0, abs=1e-9)

    def test_negative_field(self, mag, i2c):
        i2c.set_magnetic_raw(reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET - 8000, reg.ZERO_FIELD_OFFSET)
        _, y, _ = mag.magnetic
        assert y == pytest.approx(-50.0, abs=0.01)

    def test_18bit_low_bits_round_trip(self, mag, i2c):
        # value 0x20003: high byte 0x80, mid byte 0x00, low 2 bits = 0b11
        target = reg.ZERO_FIELD_OFFSET + 3
        i2c.set_magnetic_raw(target, target, target)
        x, y, z = mag.magnetic_raw
        assert x == target
        assert y == target
        assert z == target

    def test_magnetic_triggers_single_shot(self, mag, i2c):
        i2c.writes.clear()
        _ = mag.magnetic
        triggers = [w for w in i2c.writes if w[1] == reg.REG_INT_CTRL_0 and w[2] & reg.CTRL0_TM_M]
        assert len(triggers) == 1

    def test_magnetic_timeout_raises_oserror(self, mag, i2c):
        i2c.suppress_meas_done = True
        with pytest.raises(OSError, match="timed out"):
            _ = mag.magnetic


class TestTemperature:
    def test_temperature_offset_at_zero_byte(self, mag, i2c):
        i2c.set_temperature_byte(0)
        assert mag.temperature == pytest.approx(reg.TEMP_OFFSET_C, abs=0.01)

    def test_temperature_scales_correctly(self, mag, i2c):
        # Byte = 100 -> -75 + 100*0.8 = 5°C
        i2c.set_temperature_byte(100)
        assert mag.temperature == pytest.approx(5.0, abs=0.01)

    def test_temperature_timeout_raises_oserror(self, mag, i2c):
        i2c.suppress_temp_done = True
        with pytest.raises(OSError, match="timed out"):
            _ = mag.temperature


class TestReset:
    def test_reset_resets_shadow_state(self, mag, i2c):
        mag.bandwidth = 800
        assert mag.bandwidth == 800
        mag.reset()
        assert mag.bandwidth == 100
