// Shared protocol layer: JSON views of device state and the command dispatcher
// used by both BLE and HTTP, so both transports behave identically.
// Network task only.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "motion.h"
#include "wifi_manager.h"

namespace api {
void motionJson(JsonObject o, const MotionStatus& s);
void wifiJson(JsonObject o, const wifimgr::Info& i);
void testJson(JsonObject o, const wifimgr::TestResult& t);
void infoJson(JsonObject o);
void stateJson(JsonObject o);   // compact snapshot: motion + wifi + test

// cmd: {"cmd": "...", "id": n, ...}. Fills reply with {"type":"reply","id","cmd","ok",...}.
void handleCommand(JsonVariantConst cmd, JsonObject reply);

String reportHtml(const MotionStatus& s);   // v1 report text
}
