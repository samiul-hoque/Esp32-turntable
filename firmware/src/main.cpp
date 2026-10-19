// Auleek Turntable firmware v2.
//
// Core 1: loop() drives the stepper and nothing else (as tight as the v1 blocking loop).
// Core 0: the "net" task runs the web server, Wi-Fi manager and BLE event pump;
//         the Wi-Fi and NimBLE stacks run there too.
#include <Arduino.h>
#include "ble_service.h"
#include "config.h"
#include "device.h"
#include "motion.h"
#include "watchdog.h"
#include "web_ui.h"
#include "wifi_manager.h"

static void netTask(void*) {
  for (;;) {
    watchdog::beat("web");
    web::tick();
    watchdog::beat("wifi");
    wifimgr::tick();
    watchdog::beat("ble");
    ble::tick();
    watchdog::beat("idle");
    vTaskDelay(1);
  }
}

void setup() {
  // Driver disabled before anything else, exactly as v1.
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH);

  Serial.begin(115200);
  device::begin();
  Serial.printf("\n%s  firmware %s  (reset: %s)\n", device::name(), FW_VERSION, device::resetReason());
  Serial.println("System Limits:");
  Serial.println("  Max Speed: " + String(MAX_SPEED_RPM, 1) + " RPM");
  Serial.println("  Max Acceleration: " + String(MAX_ACCELERATION_RPM_S, 1) + " RPM/s");

  motion::begin();
  wifimgr::begin();   // non-blocking: the device is usable before Wi-Fi connects
  web::begin();
  ble::begin();

  watchdog::begin();
  // Idle priority on purpose: library code in this task (WebServer, Stream::timedRead)
  // busy-waits without yielding. At priority 0 FreeRTOS time-slices it with the idle
  // task, so a slow client can never starve IDLE0 into a task-watchdog reset.
  // Wi-Fi, BLE and lwIP run far above this, so networking itself is unaffected.
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

void loop() {
  motion::update();
  // While idle, give the core back (lets the idle task run); while moving, spin.
  if (!motion::runningLocal()) delay(1);
}
