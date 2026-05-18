// Compute and display compass heading.
//
// Heading angle from the X/Y components of the magnetic field. Assumes
// the sensor is held roughly level -- full 3D heading needs an IMU and
// tilt compensation, which is beyond a single-sensor example.
//
// For best accuracy, run Calibration first and substitute the hard-iron
// offsets below.

#include <Wire.h>
#include <MMC5983MA.h>

// Substitute values from Calibration for your environment.
static const float X_OFFSET_UT = 0.0f;
static const float Y_OFFSET_UT = 0.0f;

MMC5983MA mag(Wire);

static float heading_degrees(float x, float y) {
  float angle = atan2f(y, x) * (180.0f / (float)PI);
  if (angle < 0.0f) angle += 360.0f;
  return angle;
}

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

  mag.startContinuousMode(50, /*automatic_set_reset=*/true);

  Serial.println("Compass heading (degrees, 0 = north when X-axis points north):");
}

void loop() {
  if (mag.isDataReady()) {
    float x, y, z;
    if (mag.readMagneticUT(&x, &y, &z)) {
      const float heading = heading_degrees(x - X_OFFSET_UT, y - Y_OFFSET_UT);
      Serial.print("heading=");
      Serial.print(heading, 1);
      Serial.println(" deg");
    }
  }
  delay(100);
}
