#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  if (LittleFS.begin()) {
    LittleFS.format();  // wipes everything in LittleFS
    Serial.println("LittleFS formatted, all saved data removed.");
  }
}

void loop() {}
