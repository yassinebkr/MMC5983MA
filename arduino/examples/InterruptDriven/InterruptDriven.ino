// Read the magnetic field driven by the chip's INT pin instead of by
// polling. The chip pulses INT high every time MEAS_M_DONE asserts; an
// ISR sets a flag and the main loop drains the data registers on the
// next iteration.
//
// Wiring (Feather RP2040 RFM95):
//   MMC5983MA INT -> Feather A0 (GP26) (or any free interrupt-capable
//                                       digital pin; update INT_PIN below)
//   The rest is the same as BasicI2C.

#include <Wire.h>
#include <MMC5983MA.h>

static const uint8_t INT_PIN = 26;  // A0 = GP26 on Feather RP2040

MMC5983MA mag(Wire);

// Set by the ISR, cleared by the main loop after a successful read.
static volatile bool dataReadyFlag = false;

static void onDataReady() {
  dataReadyFlag = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  Wire.begin();
  Wire.setClock(400000UL);

  if (!mag.begin()) {
    Serial.print("MMC5983MA not found, lastError=");
    Serial.println((int)mag.lastError());
    while (true) {
      delay(1000);
    }
  }

  // Configure both the host GPIO interrupt and the chip's INT-on-done
  // bit in one call.
  mag.setInterruptDataReadyPin(INT_PIN, onDataReady);

  // Use continuous mode so the chip generates a steady stream of done
  // pulses; single-shot would only fire INT once per triggerSingleShotRead.
  if (!mag.startContinuousMode(50, /*automatic_set_reset=*/true)) {
    Serial.println("startContinuousMode failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Streaming at 50 Hz, interrupt-driven");
}

void loop() {
  if (!dataReadyFlag) {
    return;
  }
  dataReadyFlag = false;

  float x, y, z;
  if (!mag.readLatestMagneticUT(&x, &y, &z)) {
    Serial.print("read failed, lastError=");
    Serial.println((int)mag.lastError());
    return;
  }
  mag.clearInterrupt();  // let the chip fire INT again on the next sample

  Serial.print("X=");
  Serial.print(x, 2);
  Serial.print("\tY=");
  Serial.print(y, 2);
  Serial.print("\tZ=");
  Serial.print(z, 2);
  Serial.println(" uT");
}
