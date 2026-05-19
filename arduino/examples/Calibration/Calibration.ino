// Hard-iron offset calibration.
//
// Rotate the sensor through every orientation while this sketch runs. It
// collects min/max field on each axis and computes the hard-iron offset
// that should be subtracted from raw readings so the resulting field is
// centred on zero.
//
// This addresses the static offset from nearby ferrous material on the
// vehicle (battery clips, screws). It does not address soft-iron
// distortion -- that needs an ellipsoid fit, which does not fit in a
// demo example.

#include <Wire.h>
#include <MMC5983MA.h>

static const uint32_t CALIBRATION_SECONDS = 20;

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Wire.begin();
  Wire.setClock(400000UL);

  if (!mag.begin()) {
    Serial.println("MMC5983MA not found");
    while (true) {
      delay(1000);
    }
  }

  mag.startContinuousMode(100, /*automatic_set_reset=*/true);

  Serial.print("Rotate the sensor through every orientation for ");
  Serial.print(CALIBRATION_SECONDS);
  Serial.println(" s");
  Serial.println("Start now...");

  float x_min = INFINITY, y_min = INFINITY, z_min = INFINITY;
  float x_max = -INFINITY, y_max = -INFINITY, z_max = -INFINITY;
  const uint32_t t_end = millis() + CALIBRATION_SECONDS * 1000UL;
  while ((int32_t)(millis() - t_end) <= 0) {
    if (mag.isDataReady()) {
      float x, y, z;
      if (mag.readMagneticUT(&x, &y, &z)) {
        if (x < x_min) x_min = x;
        if (x > x_max) x_max = x;
        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
        if (z < z_min) z_min = z;
        if (z > z_max) z_max = z;
      }
    }
  }

  mag.stopContinuousMode();

  const float x_offset = (x_max + x_min) / 2.0f;
  const float y_offset = (y_max + y_min) / 2.0f;
  const float z_offset = (z_max + z_min) / 2.0f;

  Serial.println();
  Serial.println("Hard-iron offsets (subtract from raw readings):");
  Serial.print("  X: ");
  Serial.print(x_offset, 2);
  Serial.println(" uT");
  Serial.print("  Y: ");
  Serial.print(y_offset, 2);
  Serial.println(" uT");
  Serial.print("  Z: ");
  Serial.print(z_offset, 2);
  Serial.println(" uT");
  Serial.println();
  Serial.println("Range per axis (peak to peak):");
  Serial.print("  X: ");
  Serial.print(x_max - x_min, 2);
  Serial.println(" uT");
  Serial.print("  Y: ");
  Serial.print(y_max - y_min, 2);
  Serial.println(" uT");
  Serial.print("  Z: ");
  Serial.print(z_max - z_min, 2);
  Serial.println(" uT");
}

void loop() {
  delay(1000);
}
