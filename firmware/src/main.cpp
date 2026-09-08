// ESP32 Furby — firmware entry point.
//
// This is the M1 build skeleton from F-02: it proves the toolchain, the pinned platform fork
// and the local configuration path, and does nothing else. No GPIO is touched, because the
// pin map is a product of the M0 bench measurements (docs/hardware.md).
//
// F-01 replaces the body of this file with WiFi, time, logging, OTA and the watchdog.

#include <Arduino.h>

#include "config.h"
#include "furby_version.h"

namespace {

constexpr uint32_t kHeartbeatIntervalMs = 5000;

uint32_t last_heartbeat_ms = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.printf("esp32-furby %s\n", FURBY_VERSION);
  Serial.printf("host      %s\n", FURBY_HOSTNAME);
  Serial.printf("chip      %s rev %u, %u core(s) @ %lu MHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), (unsigned long)getCpuFrequencyMhz());
  Serial.printf("flash     %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
  Serial.printf("free heap %lu bytes\n", (unsigned long)ESP.getFreeHeap());
  Serial.println("skeleton build — no peripherals are driven yet");
}

void loop() {
  const uint32_t now = millis();
  if (now - last_heartbeat_ms >= kHeartbeatIntervalMs) {
    last_heartbeat_ms = now;
    Serial.printf("[%lus] alive, free heap %lu bytes\n", (unsigned long)(now / 1000),
                  (unsigned long)ESP.getFreeHeap());
  }
  delay(10);
}
