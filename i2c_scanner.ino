// Scanner I2C - upload ce sketch seul pour trouver l'adresse de ton LCD.
// Ouvre le Moniteur Serie (Ctrl+Maj+M) a 9600 bauds apres l'upload.
#include <Wire.h>

void setup() {
  Wire.begin();
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println("Scan I2C en cours...");
}

void loop() {
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Peripherique trouve a l'adresse 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) {
    Serial.println("Aucun peripherique I2C trouve - verifie le cablage (SDA=A4, SCL=A5, VCC, GND).");
  }
  Serial.println("---");
  delay(3000);
}
