// Demonstrate the async single-shot pattern: trigger a measurement,
// do other work while the chip integrates, then drain the data registers.
//
// At BW=100 Hz the chip takes ~8 ms per measurement. The synchronous
// readMagneticUT() blocks the CPU for that whole window. With the
// trigger / isDataReady / readLatest split, the same 8 ms is available
// for unrelated work (a second sensor on another bus, blinking an LED,
// running a control loop step, etc.).

#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

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

  mag.setBandwidth(100);  // ~8 ms per measurement
}

void loop() {
  // 1. Fire the measurement and return immediately.
  if (!mag.triggerSingleShotRead()) {
    Serial.print("trigger failed, lastError=");
    Serial.println((int)mag.lastError());
    delay(500);
    return;
  }

  // 2. Do useful work while the chip integrates. This is the win versus
  //    the synchronous readMagneticUT().
  uint32_t otherWorkLoops = 0;
  while (!mag.isDataReady()) {
    // ... in a real app you might service other sensors, update an
    //     attitude estimate, etc.
    otherWorkLoops++;
  }

  // 3. Drain the data registers (no measurement triggered here).
  float x, y, z;
  if (!mag.readLatestMagneticUT(&x, &y, &z)) {
    Serial.print("read failed, lastError=");
    Serial.println((int)mag.lastError());
    delay(500);
    return;
  }

  Serial.print("X=");
  Serial.print(x, 2);
  Serial.print(" Y=");
  Serial.print(y, 2);
  Serial.print(" Z=");
  Serial.print(z, 2);
  Serial.print(" uT  (");
  Serial.print(otherWorkLoops);
  Serial.println(" other-work loops while waiting)");

  delay(100);
}
