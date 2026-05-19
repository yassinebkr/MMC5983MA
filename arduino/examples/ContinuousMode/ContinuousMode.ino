// Continuous-mode reads at 100 Hz with automatic SET/RESET.
//
// Demonstrates the throughput regime intended for vehicle attitude
// estimation: data ready every 10 ms, no host-driven trigger per sample,
// periodic SET/RESET keeping the offset bounded automatically.

#include <Wire.h>
#include <MMC5983MA.h>

static const uint16_t SAMPLE_RATE_HZ = 100;
static const uint32_t RUN_SECONDS    = 30;

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  // Block until the serial host attaches. The 30 s run is short enough
  // that missing the start would lose the experiment; for an embedded
  // (host-less) deployment, remove this loop or add a millis() timeout.
  while (!Serial) {
  }

  Wire.begin();
  Wire.setClock(400000UL);

  if (!mag.begin()) {
    Serial.println("MMC5983MA not found");
    while (true) {
      delay(1000);
    }
  }

  mag.setBandwidth(400);  // comfortably above the sample rate
  if (!mag.startContinuousMode(SAMPLE_RATE_HZ, /*automatic_set_reset=*/true)) {
    Serial.println("startContinuousMode failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("Streaming at ");
  Serial.print(SAMPLE_RATE_HZ);
  Serial.print(" Hz for ");
  Serial.print(RUN_SECONDS);
  Serial.println(" s");

  uint32_t samples = 0;
  const uint32_t t_end = millis() + RUN_SECONDS * 1000UL;
  while ((int32_t)(millis() - t_end) <= 0) {
    if (mag.isDataReady()) {
      float x, y, z;
      if (mag.readMagneticUT(&x, &y, &z)) {
        ++samples;
        if (samples % SAMPLE_RATE_HZ == 0) {
          Serial.print("sample ");
          Serial.print(samples);
          Serial.print(": X=");
          Serial.print(x, 2);
          Serial.print(" Y=");
          Serial.print(y, 2);
          Serial.print(" Z=");
          Serial.print(z, 2);
          Serial.println(" uT");
        }
      }
    }
  }

  mag.stopContinuousMode();
  Serial.print("Captured ");
  Serial.print(samples);
  Serial.println(" samples");
}

void loop() {
  delay(1000);
}
