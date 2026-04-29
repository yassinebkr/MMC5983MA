# API reference — `mmc5983ma`

## `MMC5983MA(bus, *, address=0x30, cs=None, spi_baudrate=8_000_000)`

Construct a driver bound to an I²C or SPI bus.

| Parameter      | Type                          | Description                                       |
|----------------|-------------------------------|---------------------------------------------------|
| `bus`          | `busio.I2C` or `busio.SPI`    | I²C bus, or SPI bus when `cs` is supplied.        |
| `address`      | `int`                         | I²C device address. Fixed at `0x30` on the part.  |
| `cs`           | `digitalio.DigitalInOut`      | Chip-select pin. Presence selects SPI mode.       |
| `spi_baudrate` | `int`                         | SPI clock in Hz. Datasheet allows up to 10 MHz.   |

The constructor verifies the product ID, performs a software reset, and
returns the driver in single-shot mode at 100 Hz bandwidth.

Raises `RuntimeError` if no chip with PROD_ID `0x30` answers.

## Properties

### `magnetic` → `tuple[float, float, float]`
Magnetic field in **microtesla** as `(x, y, z)`. In single-shot mode the
property triggers a measurement; in continuous mode it returns the latest
reading. Raises `OSError` on timeout.

### `magnetic_raw` → `tuple[int, int, int]`
Raw 18-bit unsigned values (`0..0x3FFFF`). `0x20000` is zero field.

### `temperature` → `float`
Die temperature in **degrees Celsius** (–75 to +125 nominal). Always
single-shot. Raises `OSError` on timeout.

### `status` → `dict[str, bool]`
STATUS register snapshot with keys `magnetic_ready`, `temperature_ready`,
`otp_loaded`.

### `connected` → `bool`
`True` if the chip currently answers with the expected PROD_ID. Useful for
hot-plug or post-reset health checks.

### `bandwidth` (read/write) → `int`
Filter bandwidth in Hz: `100`, `200`, `400`, or `800`. Higher is faster and
noisier; lower is the opposite.

### `continuous_mode` → `bool` (read-only)
`True` while continuous measurement is enabled.

### `automatic_set_reset` (read/write) → `bool`
Whether the chip auto-fires SET/RESET internally between samples. Strongly
recommended for long-duration continuous mode.

### `interrupt_enabled` (read/write) → `bool`
Drives the `INT` pin high on measurement-done.

### `x_enabled`, `yz_enabled` (read/write) → `bool`
Per-channel measurement enables. Disabling unused axes saves power and time.

### `spi_3wire` (read/write) → `bool`
Switch the SPI port to 3-wire mode (bidirectional SDIO). Has no effect over
I²C.

## Methods

### `reset()`
Issue a software reset. Blocks ~15 ms while the chip restores defaults, then
clears the driver's shadow state.

### `set_coil()` / `reset_coil()`
Fire the SET or RESET coil pulse. Blocks 1 ms — the coils need that long to
settle before the next measurement is meaningful.

### `offset_canceled_read()` → `tuple[float, float, float]`
Take a SET-RESET pair of measurements and return `(M_set − M_reset) / 2` per
axis in µT. Cancels the slow internal offset that is the dominant systematic
error. Costs two measurement cycles per sample.

### `selftest()` → `bool`
Run the on-die saturation self-test. Returns `True` when every axis shows a
deflection larger than the spec floor (≈10 µT). A `False` result usually
means the sensor is in a strong DC field; check by moving it 30 cm clear of
ferrous objects.

### `configure_continuous_mode(rate_hz, *, automatic_set_reset=True)`
Start continuous mode. Valid `rate_hz`: `1, 10, 20, 50, 100, 200, 1000`.
The driver promotes `bandwidth` automatically when the rate requires it
(`200 Hz` needs ≥ 200 Hz bandwidth; `1000 Hz` needs 800 Hz).

### `configure_single_shot_mode()`
Disable continuous mode.

### `configure_periodic_set(sample_count)`
Fire a SET pulse every `sample_count` measurements. Valid counts:
`1, 25, 75, 100, 250, 500, 1000, 2000`. Smaller counts give better offset
stability at slightly higher current draw.

### `disable_periodic_set()`
Turn off periodic SET.

### `clear_interrupt(mask=…)`
Clear status flags by writing `1`s to them. `mask` defaults to clearing both
`MEAS_M_DONE` and `MEAS_T_DONE`.

## Exceptions

| Exception      | When raised                                                  |
|----------------|--------------------------------------------------------------|
| `RuntimeError` | Construction with the wrong / missing chip ID.               |
| `OSError`      | Bus failure or measurement timeout.                          |
| `ValueError`   | Out-of-range `bandwidth`, `rate_hz`, or `sample_count`.      |

## Sensor specs (reference)

| Parameter           | Value                  |
|---------------------|------------------------|
| Resolution          | 18-bit                 |
| Full-scale range    | ±8 G (±800 µT)         |
| Sensitivity         | 0.0625 mG/LSB          |
| Output rate         | up to 1000 Hz          |
| Operating voltage   | 3.0–3.6 V              |
| I²C address         | `0x30` (fixed)         |
| Self-test deflection| 80–175 mG per side     |
