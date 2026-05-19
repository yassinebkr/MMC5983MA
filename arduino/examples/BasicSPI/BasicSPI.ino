// Basic single-shot magnetic-field reading over SPI.
//
// Tested on Adafruit Feather RP2040 with RFM95 LoRa Radio. The MMC5983MA
// shares the on-board SPI bus with the RFM95 radio; only chip-select is
// unique to the magnetometer. Wire the sensor's CS to D5 (any free
// digital pin works -- update CS_PIN to match).
//
// Note: the MMC5983MA on a SparkFun Qwiic / Adafruit STEMMA QT breakout is
// strapped for I2C. To use SPI, either pick an MMC5983MA breakout that
// exposes the SPI pads (and tie SDA low at boot to select SPI on the chip),
// or wire a bare module accordingly.

#include <SPI.h>
#include <MMC5983MA.h>

static const uint8_t CS_PIN = 7;  // GP7 -> board pin D5 on Feather RP2040

MMC5983MA mag(SPI, CS_PIN, 8000000UL);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Wait briefly for USB serial; do not hang forever.
  }

  SPI.begin();

  if (!mag.begin()) {
    Serial.println("MMC5983MA not found over SPI -- check wiring, CS pin, and SPI mode strap");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("MMC5983MA online (SPI)");
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

  Serial.print("X=");
  Serial.print(x, 2);
  Serial.print("\tY=");
  Serial.print(y, 2);
  Serial.print("\tZ=");
  Serial.print(z, 2);
  Serial.println(" uT");

  delay(100);
}
