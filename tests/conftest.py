"""Shared fixtures and a register-level MMC5983MA mock for unit tests."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "circuitpython"))

from mmc5983ma import MMC5983MA  # noqa: E402
from mmc5983ma import registers as reg  # noqa: E402


def encode_xyz(x18, y18, z18):
    """Encode three 18-bit values into the chip's 7-byte output layout."""
    out = bytearray(7)
    out[0] = (x18 >> reg.XYZ_0_SHIFT) & 0xFF
    out[1] = (x18 >> reg.XYZ_1_SHIFT) & 0xFF
    out[2] = (y18 >> reg.XYZ_0_SHIFT) & 0xFF
    out[3] = (y18 >> reg.XYZ_1_SHIFT) & 0xFF
    out[4] = (z18 >> reg.XYZ_0_SHIFT) & 0xFF
    out[5] = (z18 >> reg.XYZ_1_SHIFT) & 0xFF
    out[6] = (
        ((x18 & 0x03) << reg.XYZ_X2_SHIFT)
        | ((y18 & 0x03) << reg.XYZ_Y2_SHIFT)
        | ((z18 & 0x03) << reg.XYZ_Z2_SHIFT)
    )
    return out


class MockI2C:
    """Stand-in for ``busio.I2C`` that emulates the MMC5983MA register set.

    Models the bits the driver exercises: PROD_ID, the four control
    registers, STATUS, X/Y/Z magnetic output, T_OUT temperature, and the
    one-shot trigger flow (TM_M / TM_T set the corresponding STATUS bit on
    the next poll).
    """

    def __init__(self):
        self.regs = bytearray(0x40)
        self.regs[reg.REG_PROD_ID] = reg.PROD_ID
        self.writes = []  # log of (address, register, value) for assertions
        self._mag_payload = encode_xyz(
            reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET, reg.ZERO_FIELD_OFFSET
        )
        self._temperature_byte = 100  # arbitrary default
        self.suppress_meas_done = False
        self.suppress_temp_done = False

    def set_magnetic_raw(self, x, y, z):
        self._mag_payload = encode_xyz(x, y, z)

    def set_temperature_byte(self, value):
        self._temperature_byte = value & 0xFF

    def try_lock(self):
        return True

    def unlock(self):
        pass

    def writeto(self, address, buf):
        if len(buf) < 2:
            return
        register = buf[0]
        value = buf[1]
        self.writes.append((address, register, value))
        self._handle_write(register, value)

    def writeto_then_readfrom(self, address, out_buf, in_buf):
        start = out_buf[0]
        if start == reg.REG_X_OUT_0 and len(in_buf) == 7:
            for i in range(7):
                in_buf[i] = self._mag_payload[i]
            return
        if start == reg.REG_STATUS and len(in_buf) == 1:
            # In continuous mode the chip refreshes the data and re-asserts
            # MEAS_M_DONE between reads. Mirror that here so the driver's
            # post-config wait and any subsequent status polls find data.
            s = self.regs[start]
            cmm_on = self.regs[reg.REG_INT_CTRL_2] & reg.CTRL2_CMM_EN
            if cmm_on and not self.suppress_meas_done:
                s |= reg.STATUS_MEAS_M_DONE
            in_buf[0] = s
            return
        for i in range(len(in_buf)):
            in_buf[i] = self.regs[(start + i) & 0xFF]

    def _handle_write(self, register, value):
        if register == reg.REG_INT_CTRL_1 and value & reg.CTRL1_SW_RST:
            for i in range(len(self.regs)):
                self.regs[i] = 0
            self.regs[reg.REG_PROD_ID] = reg.PROD_ID
            return

        if register == reg.REG_STATUS:
            # Write-1-to-clear semantics for the status flags.
            self.regs[register] &= ~value
            return

        if register == reg.REG_INT_CTRL_0:
            persistent = value & ~(
                reg.CTRL0_TM_M | reg.CTRL0_TM_T | reg.CTRL0_SET | reg.CTRL0_RESET
            )
            self.regs[register] = persistent
            if value & reg.CTRL0_TM_M and not self.suppress_meas_done:
                self.regs[reg.REG_STATUS] |= reg.STATUS_MEAS_M_DONE
            if value & reg.CTRL0_TM_T and not self.suppress_temp_done:
                self.regs[reg.REG_STATUS] |= reg.STATUS_MEAS_T_DONE
                self.regs[reg.REG_T_OUT] = self._temperature_byte
            return

        self.regs[register] = value


@pytest.fixture
def i2c():
    return MockI2C()


@pytest.fixture
def mag(i2c, monkeypatch):
    monkeypatch.setattr("time.sleep", lambda _seconds: None)
    return MMC5983MA(i2c)
