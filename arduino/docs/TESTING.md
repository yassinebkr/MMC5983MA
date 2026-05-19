# Testing & release verification — Arduino library

How the Arduino library is verified before a PR is merged and before a
release is tagged. Three layers:

1. **Automated CI** — every push and PR cross-compiles every example
   for three FQBNs.
2. **Local compile** — quick smoke check while iterating.
3. **Hardware bench test** — `Verify020.ino` exercises every public API
   path against a real sensor and prints PASS / FAIL per check.

If you are opening a PR that touches `arduino/`, do steps 2 and 3 before
asking for a review. If you are tagging a release, do all three.

## 1. CI (automatic, no action needed)

[`.github/workflows/arduino-compile.yml`](../../.github/workflows/arduino-compile.yml)
runs `arduino-cli compile --warnings all` against every example in
`arduino/examples/` for the following FQBNs on every push to `main` and
every PR that touches `arduino/` or the workflow itself:

| FQBN                                | Why                                                     |
|-------------------------------------|---------------------------------------------------------|
| `rp2040:rp2040:adafruit_feather_rfm`| The board the library is hardware-validated on.        |
| `arduino:avr:uno`                   | Smallest target — catches accidental C++17-only usage.  |
| `esp32:esp32:esp32`                 | Different toolchain — catches Wire/SPI assumptions.     |

These are the same compile checks the
[Arduino Library Manager validator](https://github.com/arduino/library-registry)
runs on a registry submission, so anything green here is guaranteed to
pass the registry bot.

Check status:
https://github.com/yassinebkr/MMC5983MA/actions/workflows/arduino-compile.yml

## 2. Local compile

Install `arduino-cli` and the Earle Philhower RP2040 core (one-time):

```bash
# Windows
winget install ArduinoSA.CLI

# macOS
brew install arduino-cli

# Linux
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli core update-index
arduino-cli core install rp2040:rp2040
```

Compile any single example from the repo root:

```bash
arduino-cli compile --warnings all \
  --fqbn rp2040:rp2040:adafruit_feather_rfm \
  --library arduino \
  arduino/examples/BasicI2C
```

Compile every example (PowerShell — adapt for bash):

```powershell
$cli = "arduino-cli"
"BasicI2C","BasicSPI","Selftest","ContinuousMode","Calibration",
"HeadingCompass","InterruptDriven","AsyncRead","Verify020" | ForEach-Object {
  Write-Output "==== $_ ===="
  & $cli compile --warnings all `
    --fqbn rp2040:rp2040:adafruit_feather_rfm `
    --library arduino "arduino\examples\$_"
}
```

A clean compile prints sketch size and zero warnings. The library is
expected to land at roughly 60-72 KB program / 9-10 KB RAM on RP2040.

## 3. Hardware bench test — `Verify020.ino`

This is the single sketch that proves the library actually works on
silicon. It is mandatory before tagging any release.

**Hardware:** Feather RP2040 RFM95 with an MMC5983MA breakout on the
STEMMA QT / I²C bus (SDA → GP2, SCL → GP3, VCC → 3V3, GND → GND).

**Procedure:**

1. Open `File → Examples → MMC5983MA → Verify020` in the Arduino IDE.
   (Or `arduino-cli compile --upload --port <COM> --fqbn rp2040:rp2040:adafruit_feather_rfm --library arduino arduino/examples/Verify020`.)
2. Upload.
3. Open the Serial Monitor at **115200 baud**. The sketch blocks on
   serial; output starts within a second of opening the monitor.
4. Confirm the final line reads **`ALL TESTS PASS`** (14 of 14 in the
   current build). Save the full transcript with the PR or release.

**What the sketch covers:**

| Test | Path                                                        |
|------|-------------------------------------------------------------|
| 1    | `begin()` + `lastError() == None`                          |
| 2    | `setBandwidth(invalid)` + `lastError() == InvalidArgument` |
| 3    | Synchronous `readMagneticUT` in 20-80 µT                   |
| 4    | `triggerSingleShotRead` + `isDataReady` + `readLatestMagneticUT` matches sync within 5 µT |
| 5-7  | `readMagneticAxisUT(X/Y/Z)` matches a fresh sync read       |
| 8-9  | `setXEnabled` / `setYZEnabled` round-trip through shadows  |
| 10   | Read still works after re-enabling channels                |
| 11   | `triggerSingleShotRead` no-op in continuous mode           |
| 12   | `readLatestMagneticUT` in continuous mode                  |

**Not covered here** (run as separate sketches if relevant to the change):

- SPI bus path — re-wire to SPI (`CS` on D5/GP7, `SCL` → SCK, `SDA` →
  MOSI, `SDO` → MISO) and upload `BasicSPI.ino`. SPI is currently
  compile-clean but not hardware-validated.
- INT pin path — wire MMC5983MA `INT` to a Feather GPIO and upload
  `InterruptDriven.ino`. The chip's interrupt-on-done is only verified
  on hardware; CI cannot exercise it.
- Continuous-mode stress run — upload `ContinuousMode.ino` to confirm
  the 30 s capture produces ≥ 3000 samples with no garbage first
  sample (the quirk #2 fix).

## Release flow (maintainers)

The Arduino library version lives in
[`arduino/library.properties`](../library.properties) (`version=...`).

1. **Land all behavior changes on `main`** via PR. CI must be green on
   all three FQBNs on the merge commit.
2. **Bump `version=`** in `arduino/library.properties` on a release
   branch. Follow [semver](https://semver.org/): patch for bug fixes,
   minor for backward-compatible additions, major for breaking
   changes.
3. **Run `Verify020.ino`** on real hardware. Save the transcript.
4. **Open and merge the version-bump PR.** CI must be green.
5. **Tag the merge commit on `main`** with a tag name that exactly
   matches `version=`:
   ```bash
   git tag 0.2.0
   git push origin 0.2.0
   ```
6. Library Manager auto-picks up new tags on registered repos (see
   `arduino/library.properties` for registration state).
