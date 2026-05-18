// Register map for the MEMSIC MMC5983MA 3-axis magnetometer.
//
// Addresses, bit fields, and timing constants live here so the driver in
// MMC5983MA.cpp never carries a magic number. Cross-checked against the
// SparkFun Arduino library, the MEMSIC datasheet (Rev D), and the
// hardware-validated CircuitPython port in ../../circuitpython.

#ifndef MMC5983MA_REGISTERS_H
#define MMC5983MA_REGISTERS_H

#include <stdint.h>

namespace mmc5983ma {

// ---------------------------------------------------------------------------
// Bus addresses
// ---------------------------------------------------------------------------

constexpr uint8_t I2C_ADDRESS = 0x30;

// Read mask for the SPI command byte. MMC5983MA uses MSB=1 for read,
// MSB=0 for write, with the lower 7 bits as the register address.
constexpr uint8_t SPI_READ  = 0x80;
constexpr uint8_t SPI_WRITE = 0x00;

// ---------------------------------------------------------------------------
// Register addresses
// ---------------------------------------------------------------------------

constexpr uint8_t REG_X_OUT_0    = 0x00;
constexpr uint8_t REG_X_OUT_1    = 0x01;
constexpr uint8_t REG_Y_OUT_0    = 0x02;
constexpr uint8_t REG_Y_OUT_1    = 0x03;
constexpr uint8_t REG_Z_OUT_0    = 0x04;
constexpr uint8_t REG_Z_OUT_1    = 0x05;
constexpr uint8_t REG_XYZ_OUT_2  = 0x06;
constexpr uint8_t REG_T_OUT      = 0x07;
constexpr uint8_t REG_STATUS     = 0x08;
constexpr uint8_t REG_INT_CTRL_0 = 0x09;
constexpr uint8_t REG_INT_CTRL_1 = 0x0A;
constexpr uint8_t REG_INT_CTRL_2 = 0x0B;
constexpr uint8_t REG_INT_CTRL_3 = 0x0C;
constexpr uint8_t REG_PROD_ID    = 0x2F;

// Expected value of REG_PROD_ID. Used as a "who am I" sanity check.
constexpr uint8_t PROD_ID = 0x30;

// ---------------------------------------------------------------------------
// STATUS register (0x08) bit fields
// ---------------------------------------------------------------------------

constexpr uint8_t STATUS_MEAS_M_DONE   = 1 << 0;  // magnetic measurement complete
constexpr uint8_t STATUS_MEAS_T_DONE   = 1 << 1;  // temperature measurement complete
constexpr uint8_t STATUS_OTP_READ_DONE = 1 << 4;  // one-time programmable memory loaded

// ---------------------------------------------------------------------------
// INT_CTRL_0 register (0x09) bit fields
// ---------------------------------------------------------------------------

constexpr uint8_t CTRL0_TM_M             = 1 << 0;  // trigger one-shot magnetic measurement
constexpr uint8_t CTRL0_TM_T             = 1 << 1;  // trigger one-shot temperature measurement
constexpr uint8_t CTRL0_INT_MEAS_DONE_EN = 1 << 2;  // raise INT pin on measurement done
constexpr uint8_t CTRL0_SET              = 1 << 3;  // fire SET coil pulse
constexpr uint8_t CTRL0_RESET            = 1 << 4;  // fire RESET coil pulse
constexpr uint8_t CTRL0_AUTO_SR_EN       = 1 << 5;  // automatic SET/RESET in continuous mode
constexpr uint8_t CTRL0_OTP_READ         = 1 << 6;  // re-read OTP shadow registers

// ---------------------------------------------------------------------------
// INT_CTRL_1 register (0x0A) bit fields
// ---------------------------------------------------------------------------

constexpr uint8_t CTRL1_BW0        = 1 << 0;
constexpr uint8_t CTRL1_BW1        = 1 << 1;
constexpr uint8_t CTRL1_BW_MASK    = 0x03;
constexpr uint8_t CTRL1_X_INHIBIT  = 1 << 2;  // disable X channel measurement
constexpr uint8_t CTRL1_YZ_INHIBIT = 3 << 3;  // disable Y and Z channels (both bits set)
constexpr uint8_t CTRL1_SW_RST     = 1 << 7;  // software reset; self-clears

// Bandwidth field encodings (BW[1:0]). Higher bandwidth = faster, noisier.
constexpr uint8_t BW_100_HZ = 0x00;  // measurement time ~8.0 ms
constexpr uint8_t BW_200_HZ = 0x01;  // measurement time ~4.0 ms
constexpr uint8_t BW_400_HZ = 0x02;  // measurement time ~2.0 ms
constexpr uint8_t BW_800_HZ = 0x03;  // measurement time ~0.5 ms

// ---------------------------------------------------------------------------
// INT_CTRL_2 register (0x0B) bit fields
// ---------------------------------------------------------------------------

constexpr uint8_t CTRL2_CM_FREQ_MASK  = 0x07;  // bits [2:0] continuous mode rate
constexpr uint8_t CTRL2_CMM_EN        = 1 << 3;  // enable continuous measurement mode
constexpr uint8_t CTRL2_PRD_SET_MASK  = 0x70;  // bits [6:4] periodic SET sample count
constexpr uint8_t CTRL2_PRD_SET_SHIFT = 4;
constexpr uint8_t CTRL2_EN_PRD_SET    = 1 << 7;  // enable periodic SET feature

// Continuous mode frequency encodings (CM_FREQ[2:0]).
constexpr uint8_t CM_FREQ_OFF     = 0x00;
constexpr uint8_t CM_FREQ_1_HZ    = 0x01;
constexpr uint8_t CM_FREQ_10_HZ   = 0x02;
constexpr uint8_t CM_FREQ_20_HZ   = 0x03;
constexpr uint8_t CM_FREQ_50_HZ   = 0x04;
constexpr uint8_t CM_FREQ_100_HZ  = 0x05;
constexpr uint8_t CM_FREQ_200_HZ  = 0x06;  // requires BW >= 200 Hz
constexpr uint8_t CM_FREQ_1000_HZ = 0x07;  // requires BW = 800 Hz

// Periodic SET sample-count encodings (PRD_SET[2:0]).
constexpr uint8_t PRD_SET_1    = 0x00;
constexpr uint8_t PRD_SET_25   = 0x01;
constexpr uint8_t PRD_SET_75   = 0x02;
constexpr uint8_t PRD_SET_100  = 0x03;
constexpr uint8_t PRD_SET_250  = 0x04;
constexpr uint8_t PRD_SET_500  = 0x05;
constexpr uint8_t PRD_SET_1000 = 0x06;
constexpr uint8_t PRD_SET_2000 = 0x07;

// ---------------------------------------------------------------------------
// INT_CTRL_3 register (0x0C) bit fields
// ---------------------------------------------------------------------------

constexpr uint8_t CTRL3_ST_ENP = 1 << 1;  // apply extra current positive->negative (selftest)
constexpr uint8_t CTRL3_ST_ENM = 1 << 2;  // apply extra current negative->positive (selftest)
constexpr uint8_t CTRL3_SPI_3W = 1 << 6;  // enable 3-wire SPI mode (SDIO bidirectional)

// ---------------------------------------------------------------------------
// XYZ_OUT_2 register (0x06) bit-packing
// ---------------------------------------------------------------------------
//
// The MMC5983MA produces 18-bit unsigned readings per axis. The two MSB
// registers hold bits [17:10] and [9:2] of each axis; the lowest 2 bits of
// each axis are packed into XYZ_OUT_2 as:
//
//   bit 7:6 -> X[1:0]
//   bit 5:4 -> Y[1:0]
//   bit 3:2 -> Z[1:0]
//   bit 1:0 -> reserved
//
// Reconstruction:
//   raw = (OUT_0 << XYZ_0_SHIFT) | (OUT_1 << XYZ_1_SHIFT) | ((OUT_2 >> shift) & 0x03)

constexpr uint8_t XYZ_X2_SHIFT = 6;
constexpr uint8_t XYZ_Y2_SHIFT = 4;
constexpr uint8_t XYZ_Z2_SHIFT = 2;
constexpr uint8_t XYZ_2_MASK   = 0x03;
constexpr uint8_t XYZ_0_SHIFT  = 10;
constexpr uint8_t XYZ_1_SHIFT  = 2;

// Zero-field offset for the unsigned 18-bit output. Output range is
// 0..0x3FFFF; 0x20000 corresponds to 0 gauss.
constexpr uint32_t ZERO_FIELD_OFFSET = 131072UL;  // 0x20000

// ---------------------------------------------------------------------------
// Conversion constants
// ---------------------------------------------------------------------------
//
// 18-bit resolution over +/-8 gauss = 16 G / 262144 LSB ~= 0.0625 mG/LSB.
// Convert to microtesla: 1 mG = 0.1 uT, so 0.0625 mG/LSB = 0.00625 uT/LSB.

constexpr float SENSITIVITY_MG_PER_LSB    = 0.0625f;
constexpr float SENSITIVITY_UT_PER_LSB    = 0.00625f;
constexpr float SENSITIVITY_GAUSS_PER_LSB = 6.25e-5f;

// Temperature: 0.8 C/LSB with 0x00 corresponding to -75 C.
// Range is approximately -75 C to +125 C.
constexpr float TEMP_OFFSET_C = -75.0f;
constexpr float TEMP_LSB_C    = 0.8f;

// ---------------------------------------------------------------------------
// Timing constants (milliseconds)
// ---------------------------------------------------------------------------
//
// Values come from the SparkFun Arduino library, which adds margin over the
// minimums in the datasheet. We follow that library exactly so register-level
// behavior matches a known-good reference.

constexpr uint16_t RESET_DELAY_MS           = 15;  // datasheet says 10 ms; use 15 for margin
constexpr uint16_t SET_RESET_PULSE_DELAY_MS = 1;   // datasheet minimum is ~500 ns
constexpr uint16_t TEMP_TIMEOUT_MS          = 5;   // max time to wait for T_DONE
constexpr uint16_t MEAS_POLL_INTERVAL_MS    = 1;   // poll period when waiting for M_DONE

// Per-bandwidth measurement timeout (ms). Includes margin over the typical
// measurement time so the driver does not give up on a healthy sensor.
constexpr uint16_t MEAS_TIMEOUT_MS_BW_100 = 32;
constexpr uint16_t MEAS_TIMEOUT_MS_BW_200 = 16;
constexpr uint16_t MEAS_TIMEOUT_MS_BW_400 = 8;
constexpr uint16_t MEAS_TIMEOUT_MS_BW_800 = 4;

// Selftest pass threshold (microtesla). Datasheet "Applied SAT field" is
// min 80 mG, typ 125 mG per side, so the ST_ENP-to-ST_ENM swing must exceed
// 160 mG even on a worst-case part. 10 uT (= 100 mG) sits comfortably below
// the spec minimum but well above the sensor's noise floor.
constexpr float SELFTEST_THRESHOLD_UT = 10.0f;

}  // namespace mmc5983ma

#endif  // MMC5983MA_REGISTERS_H
