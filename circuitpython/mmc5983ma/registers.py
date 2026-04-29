"""Register map for the MEMSIC MMC5983MA 3-axis magnetometer.

Addresses, bit fields, and timing constants are kept here so the driver in
``__init__.py`` never carries a magic number. Cross-checked against the
SparkFun Arduino library and the MEMSIC datasheet (Rev D).
"""

from micropython import const

# ---------------------------------------------------------------------------
# Bus addresses
# ---------------------------------------------------------------------------

I2C_ADDRESS = const(0x30)

# Read mask for the SPI command byte. The MMC5983MA uses MSB=1 for read,
# MSB=0 for write, with the lower 7 bits as the register address.
SPI_READ = const(0x80)
SPI_WRITE = const(0x00)

# ---------------------------------------------------------------------------
# Register addresses
# ---------------------------------------------------------------------------

REG_X_OUT_0 = const(0x00)
REG_X_OUT_1 = const(0x01)
REG_Y_OUT_0 = const(0x02)
REG_Y_OUT_1 = const(0x03)
REG_Z_OUT_0 = const(0x04)
REG_Z_OUT_1 = const(0x05)
REG_XYZ_OUT_2 = const(0x06)
REG_T_OUT = const(0x07)
REG_STATUS = const(0x08)
REG_INT_CTRL_0 = const(0x09)
REG_INT_CTRL_1 = const(0x0A)
REG_INT_CTRL_2 = const(0x0B)
REG_INT_CTRL_3 = const(0x0C)
REG_PROD_ID = const(0x2F)

# Expected value of REG_PROD_ID. Used as a "who am I" sanity check.
PROD_ID = const(0x30)

# ---------------------------------------------------------------------------
# STATUS register (0x08) bit fields
# ---------------------------------------------------------------------------

STATUS_MEAS_M_DONE = const(1 << 0)   # magnetic measurement complete
STATUS_MEAS_T_DONE = const(1 << 1)   # temperature measurement complete
STATUS_OTP_READ_DONE = const(1 << 4)  # one-time programmable memory loaded

# ---------------------------------------------------------------------------
# INT_CTRL_0 register (0x09) bit fields
# ---------------------------------------------------------------------------

CTRL0_TM_M = const(1 << 0)              # trigger one-shot magnetic measurement
CTRL0_TM_T = const(1 << 1)              # trigger one-shot temperature measurement
CTRL0_INT_MEAS_DONE_EN = const(1 << 2)  # raise INT pin on measurement done
CTRL0_SET = const(1 << 3)               # fire SET coil pulse
CTRL0_RESET = const(1 << 4)             # fire RESET coil pulse
CTRL0_AUTO_SR_EN = const(1 << 5)        # automatic SET/RESET in continuous mode
CTRL0_OTP_READ = const(1 << 6)          # re-read OTP shadow registers

# ---------------------------------------------------------------------------
# INT_CTRL_1 register (0x0A) bit fields
# ---------------------------------------------------------------------------

CTRL1_BW0 = const(1 << 0)
CTRL1_BW1 = const(1 << 1)
CTRL1_BW_MASK = const(0x03)
CTRL1_X_INHIBIT = const(1 << 2)   # disable X channel measurement
CTRL1_YZ_INHIBIT = const(3 << 3)  # disable Y and Z channels (both bits set)
CTRL1_SW_RST = const(1 << 7)      # software reset; self-clears

# Bandwidth field encodings (BW[1:0]). Higher bandwidth = faster, noisier.
BW_100_HZ = const(0x00)  # measurement time ~8.0 ms
BW_200_HZ = const(0x01)  # measurement time ~4.0 ms
BW_400_HZ = const(0x02)  # measurement time ~2.0 ms
BW_800_HZ = const(0x03)  # measurement time ~0.5 ms

# ---------------------------------------------------------------------------
# INT_CTRL_2 register (0x0B) bit fields
# ---------------------------------------------------------------------------

CTRL2_CM_FREQ_MASK = const(0x07)   # bits [2:0] continuous mode rate
CTRL2_CMM_EN = const(1 << 3)       # enable continuous measurement mode
CTRL2_PRD_SET_MASK = const(0x70)   # bits [6:4] periodic SET sample count
CTRL2_PRD_SET_SHIFT = const(4)
CTRL2_EN_PRD_SET = const(1 << 7)   # enable periodic SET feature

# Continuous mode frequency encodings (CM_FREQ[2:0]).
CM_FREQ_OFF = const(0x00)
CM_FREQ_1_HZ = const(0x01)
CM_FREQ_10_HZ = const(0x02)
CM_FREQ_20_HZ = const(0x03)
CM_FREQ_50_HZ = const(0x04)
CM_FREQ_100_HZ = const(0x05)
CM_FREQ_200_HZ = const(0x06)   # requires BW >= 200 Hz
CM_FREQ_1000_HZ = const(0x07)  # requires BW = 800 Hz

# Periodic SET sample-count encodings (PRD_SET[2:0]).
PRD_SET_1 = const(0x00)
PRD_SET_25 = const(0x01)
PRD_SET_75 = const(0x02)
PRD_SET_100 = const(0x03)
PRD_SET_250 = const(0x04)
PRD_SET_500 = const(0x05)
PRD_SET_1000 = const(0x06)
PRD_SET_2000 = const(0x07)

# ---------------------------------------------------------------------------
# INT_CTRL_3 register (0x0C) bit fields
# ---------------------------------------------------------------------------

CTRL3_ST_ENP = const(1 << 1)   # apply extra current positive→negative (selftest)
CTRL3_ST_ENM = const(1 << 2)   # apply extra current negative→positive (selftest)
CTRL3_SPI_3W = const(1 << 6)   # enable 3-wire SPI mode (SDIO bidirectional)

# ---------------------------------------------------------------------------
# XYZ_OUT_2 register (0x06) bit-packing
# ---------------------------------------------------------------------------
#
# The MMC5983MA produces 18-bit unsigned readings per axis. The two MSB
# registers hold bits [17:10] and [9:2] of each axis; the lowest 2 bits of
# each axis are packed into XYZ_OUT_2 as:
#
#   bit 7:6 -> X[1:0]
#   bit 5:4 -> Y[1:0]
#   bit 3:2 -> Z[1:0]
#   bit 1:0 -> reserved
#
# Reconstruction:
#   raw = (OUT_0 << XYZ_0_SHIFT) | (OUT_1 << XYZ_1_SHIFT) | ((OUT_2 >> shift) & 0x03)

XYZ_X2_SHIFT = const(6)
XYZ_Y2_SHIFT = const(4)
XYZ_Z2_SHIFT = const(2)
XYZ_2_MASK = const(0x03)
XYZ_0_SHIFT = const(10)
XYZ_1_SHIFT = const(2)

# Zero-field offset for the unsigned 18-bit output. Output range is
# 0..0x3FFFF; 0x20000 corresponds to 0 gauss.
ZERO_FIELD_OFFSET = const(131072)  # 0x20000

# ---------------------------------------------------------------------------
# Conversion constants
# ---------------------------------------------------------------------------
#
# 18-bit resolution over ±8 gauss = 16 G / 262144 LSB ≈ 0.0625 mG/LSB.
# Convert to microtesla: 1 mG = 0.1 µT, so 0.0625 mG/LSB = 0.00625 µT/LSB.
# Stored as a float because const() does not distinguish int from float here
# but readers should treat it as float.

SENSITIVITY_MG_PER_LSB = 0.0625
SENSITIVITY_UT_PER_LSB = 0.00625
SENSITIVITY_GAUSS_PER_LSB = 6.25e-5

# Temperature: 0.8 °C/LSB with 0x00 corresponding to -75 °C.
# Range is approximately -75 °C to +125 °C.
TEMP_OFFSET_C = -75.0
TEMP_LSB_C = 0.8

# ---------------------------------------------------------------------------
# Timing constants (milliseconds)
# ---------------------------------------------------------------------------
#
# Values come from the SparkFun Arduino library, which adds margin over the
# minimums in the datasheet. We follow that library exactly so register-level
# behavior matches a known-good reference.

RESET_DELAY_MS = const(15)             # datasheet says 10 ms; use 15 for margin
SET_RESET_PULSE_DELAY_MS = const(1)    # datasheet minimum is ~500 ns
TEMP_TIMEOUT_MS = const(5)             # max time to wait for T_DONE
MEAS_POLL_INTERVAL_MS = const(1)       # poll period when waiting for M_DONE

# Per-bandwidth measurement timeout (ms). Includes margin over the typical
# measurement time so the driver does not give up on a healthy sensor.
MEAS_TIMEOUT_MS_BW_100 = const(32)
MEAS_TIMEOUT_MS_BW_200 = const(16)
MEAS_TIMEOUT_MS_BW_400 = const(8)
MEAS_TIMEOUT_MS_BW_800 = const(4)
