// Basic single-shot magnetic-field reading over I2C.
//
// Tested on Adafruit Feather RP2040 with RFM95 LoRa Radio. The MMC5983MA is
// wired to the STEMMA QT connector (or to SDA/SCL) and runs from 3.3 V.

#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Wait briefly for the USB serial host to attach; do not hang forever.
  }

  Wire.begin();
  Wire.setClock(400000UL);

  if (!mag.begin()) {
    Serial.println("MMC5983MA not found at 0x30 -- check wiring and power");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("MMC5983MA online");
  Serial.print("Bandwidth: ");
  Serial.print(mag.getBandwidth());
  Serial.println(" Hz");
  Serial.print("Self-test: ");
  Serial.println(mag.runSelftest() ? "PASS" : "FAIL");
}

void loop() {
  float x, y, z;
  if (!mag.readMagneticUT(&x, &y, &z)) {
    Serial.println("read failed");
    delay(500);
    return;
  }
  const float magnitude = sqrtf(x * x + y * y + z * z);

  Serial.print("X=");
  Serial.print(x, 2);
  Serial.print("\tY=");
  Serial.print(y, 2);
  Serial.print("\tZ=");
  Serial.print(z, 2);
  Serial.print("\t|B|=");
  Serial.print(magnitude, 2);
  Serial.println(" uT");

  delay(100);
}
