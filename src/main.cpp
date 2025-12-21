#include <Arduino.h>

void setup()
{
  Serial.begin(115200);
  delay(1000); // give serial time to attach

  Serial.println("Hello, ESP32!");
}

void loop() {}
