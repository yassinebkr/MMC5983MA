// 0.2.0 verification sketch.
//
// Exercises every new API path added in 0.2.0 against the real chip and
// prints PASS/FAIL for each check. Upload once, watch the serial monitor
// at 115200 baud, confirm "ALL TESTS PASS" before tagging 0.2.0 for
// Library Manager release.
//
// Wiring: same as BasicI2C (sensor on STEMMA QT / I2C). InterruptDriven
// is not exercised here -- that one needs the chip's INT pin wired to
// a Feather GPIO and is verified by uploading InterruptDriven.ino
// separately.

#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

static int pass_count = 0;
static int fail_count = 0;

static void report(const char* name, bool ok, const char* detail = nullptr) {
  Serial.print(ok ? "[PASS] " : "[FAIL] ");
  Serial.print(name);
  if (detail != nullptr) {
    Serial.print(" -- ");
    Serial.print(detail);
  }
  Serial.println();
  if (ok) pass_count++;
  else    fail_count++;
}

static const char* errorName(MMC5983MA::Error e) {
  switch (e) {
    case MMC5983MA::Error::None:               return "None";
    case MMC5983MA::Error::BusTimeout:         return "BusTimeout";
    case MMC5983MA::Error::BusNack:            return "BusNack";
    case MMC5983MA::Error::IdMismatch:         return "IdMismatch";
    case MMC5983MA::Error::MeasurementTimeout: return "MeasurementTimeout";
    case MMC5983MA::Error::InvalidArgument:    return "InvalidArgument";
  }
  return "?";
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
  }

  Wire.begin();
  Wire.setClock(400000UL);

  Serial.println();
  Serial.println("=== MMC5983MA 0.2.0 verification ===");

  // ----- begin() should succeed -----
  bool b = mag.begin();
  char buf[64];
  snprintf(buf, sizeof(buf), "lastError=%s", errorName(mag.lastError()));
  report("Test 1: begin()", b && mag.lastError() == MMC5983MA::Error::None, buf);
  if (!b) {
    Serial.println("Cannot continue without begin(). Halting.");
    while (true) delay(1000);
  }

  // ----- lastError after invalid setBandwidth -----
  bool bw_ok = mag.setBandwidth(123);  // 123 Hz is not valid
  bool t2 = (bw_ok == false) && (mag.lastError() == MMC5983MA::Error::InvalidArgument);
  snprintf(buf, sizeof(buf), "returned=%d lastError=%s", (int)bw_ok, errorName(mag.lastError()));
  report("Test 2: lastError() == InvalidArgument", t2, buf);

  // Restore a valid bandwidth so later tests run clean.
  mag.setBandwidth(100);

  // ----- baseline synchronous read -----
  float sx, sy, sz;
  bool sync_ok = mag.readMagneticUT(&sx, &sy, &sz);
  float sync_mag = sqrtf(sx*sx + sy*sy + sz*sz);
  snprintf(buf, sizeof(buf), "|B|=%.2f uT", sync_mag);
  report("Test 3: sync readMagneticUT", sync_ok && sync_mag > 20.0f && sync_mag < 80.0f, buf);

  // ----- async trigger / wait / readLatest -----
  bool trig_ok = mag.triggerSingleShotRead();
  uint32_t spin = 0;
  while (!mag.isDataReady() && spin < 200000) {
    spin++;
  }
  float ax, ay, az;
  bool async_read_ok = mag.readLatestMagneticUT(&ax, &ay, &az);
  float async_mag = sqrtf(ax*ax + ay*ay + az*az);
  float diff = fabsf(async_mag - sync_mag);
  bool t4 = trig_ok && async_read_ok && diff < 5.0f;  // 5 uT slack for orientation drift between calls
  snprintf(buf, sizeof(buf), "|B|=%.2f uT spin=%lu diff=%.2f uT", async_mag, (unsigned long)spin, diff);
  report("Test 4: async trigger+wait+read matches sync", t4, buf);

  // ----- per-axis reads -----
  float px, py, pz;
  bool axis_x = mag.readMagneticAxisUT(MMC5983MA::Axis::X, &px);
  bool axis_y = mag.readMagneticAxisUT(MMC5983MA::Axis::Y, &py);
  bool axis_z = mag.readMagneticAxisUT(MMC5983MA::Axis::Z, &pz);
  // Compare to a fresh sync read (orientation may have shifted vs sx,sy,sz).
  float rx, ry, rz;
  mag.readMagneticUT(&rx, &ry, &rz);
  bool t5 = axis_x && fabsf(px - rx) < 3.0f;
  bool t6 = axis_y && fabsf(py - ry) < 3.0f;
  bool t7 = axis_z && fabsf(pz - rz) < 3.0f;
  snprintf(buf, sizeof(buf), "axisX=%.2f sync=%.2f", px, rx);
  report("Test 5: readMagneticAxisUT(X)", t5, buf);
  snprintf(buf, sizeof(buf), "axisY=%.2f sync=%.2f", py, ry);
  report("Test 6: readMagneticAxisUT(Y)", t6, buf);
  snprintf(buf, sizeof(buf), "axisZ=%.2f sync=%.2f", pz, rz);
  report("Test 7: readMagneticAxisUT(Z)", t7, buf);

  // ----- per-channel inhibits -----
  // Just verify the API succeeds and the shadow getters reflect the change.
  // The chip's behavior for a disabled axis (zero / hold last value /
  // undefined) is not strictly specified, so don't assert on the data.
  mag.setXEnabled(false);
  bool t8 = (mag.isXEnabled() == false) && (mag.lastError() == MMC5983MA::Error::None);
  report("Test 8: setXEnabled(false) -> isXEnabled() false", t8);

  mag.setXEnabled(true);
  bool t8b = (mag.isXEnabled() == true);
  report("Test 8b: setXEnabled(true) -> isXEnabled() true", t8b);

  mag.setYZEnabled(false);
  bool t9 = (mag.isYZEnabled() == false) && (mag.lastError() == MMC5983MA::Error::None);
  report("Test 9: setYZEnabled(false) -> isYZEnabled() false", t9);

  mag.setYZEnabled(true);
  bool t9b = (mag.isYZEnabled() == true);
  report("Test 9b: setYZEnabled(true) -> isYZEnabled() true", t9b);

  // ----- read after re-enabling all channels still works -----
  float qx, qy, qz;
  bool post_enable_ok = mag.readMagneticUT(&qx, &qy, &qz);
  float post_mag = sqrtf(qx*qx + qy*qy + qz*qz);
  bool t10 = post_enable_ok && post_mag > 20.0f && post_mag < 80.0f;
  snprintf(buf, sizeof(buf), "|B|=%.2f uT", post_mag);
  report("Test 10: read after channel re-enable", t10, buf);

  // ----- async path in continuous mode (triggerSingleShotRead is a no-op) -----
  mag.startContinuousMode(100, true);
  bool trig_cm_ok = mag.triggerSingleShotRead();
  bool t11 = trig_cm_ok && (mag.lastError() == MMC5983MA::Error::None);
  report("Test 11: triggerSingleShotRead() is no-op in continuous mode", t11);

  // Drain a few continuous-mode samples via readLatest.
  delay(20);
  float cx, cy, cz;
  bool latest_ok = mag.readLatestMagneticUT(&cx, &cy, &cz);
  float latest_mag = sqrtf(cx*cx + cy*cy + cz*cz);
  bool t12 = latest_ok && latest_mag > 20.0f && latest_mag < 80.0f;
  snprintf(buf, sizeof(buf), "|B|=%.2f uT", latest_mag);
  report("Test 12: readLatestMagneticUT in continuous mode", t12, buf);

  mag.stopContinuousMode();

  // ----- Summary -----
  Serial.println();
  Serial.print("=== ");
  Serial.print(pass_count);
  Serial.print(" passed, ");
  Serial.print(fail_count);
  Serial.print(" failed");
  if (fail_count == 0) {
    Serial.println(" -- ALL TESTS PASS ===");
  } else {
    Serial.println(" -- REVIEW FAILURES BEFORE TAGGING ===");
  }
}

void loop() {
  delay(5000);
}
