// Run the built-in self-test and print a verdict.
//
// The MMC5983MA's on-die self-test injects a known current to deflect the
// sensor by 80-175 mG per side. The driver compares deflections under
// positive and negative selftest current and reports a pass when every
// axis exceeds the spec floor.
//
// The verdict prints once on startup and then re-prints every 3 s so the
// result is visible no matter when the serial monitor is attached.

#include <Wire.h>
#include <MMC5983MA.h>

MMC5983MA mag(Wire);

void setup() {
  Serial.begin(115200);
  // Block forever waiting for a serial host. Selftest is a diagnostic
  // sketch; nobody runs it unattended.
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

  Serial.print("Connected: ");
  Serial.println(mag.isConnected() ? "yes" : "no");
  Serial.print("Bandwidth: ");
  Serial.print(mag.getBandwidth());
  Serial.println(" Hz");
}

void loop() {
  const bool ok = mag.runSelftest();
  if (ok) {
    Serial.println("SELFTEST: PASS");
  } else {
    Serial.println("SELFTEST: FAIL -- sensor may be damaged or in a strong DC field");
  }
  delay(3000);
}
