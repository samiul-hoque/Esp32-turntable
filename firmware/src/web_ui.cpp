#include <Arduino.h>
#include <WiFi.h>
#include "api.h"
#include "ble_service.h"
#include "config.h"
#include "ota.h"
#include "web_page.h"
#include "web_ui.h"
#include "watchdog.h"

static WebServer s_server(80);

static void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json", out);
}

// Keep Wi-Fi and BLE serviced while an HTTP handler waits (the web server
// itself is not re-entrant, so it is the only thing paused).
static void netYield() {
  watchdog::beat("web:wait");
  wifimgr::tick();
  ble::tick();
  vTaskDelay(pdMS_TO_TICKS(10));
}

static bool argPresent(const char* name) {
  return s_server.hasArg(name) && s_server.arg(name).length() > 0;
}

// Same parsing and defaults as v1 handleRotate().
static RotateRequest requestFromArgs() {
  RotateRequest r;
  r.angle = s_server.arg("angle").toInt();
  r.timeMs = s_server.arg("time").toInt();
  r.reverse = s_server.arg("direction") == "reverse";
  r.repetitions = s_server.hasArg("repetitions") ? s_server.arg("repetitions").toInt() : 1;
  r.repDelayMs = s_server.hasArg("repDelay") ? s_server.arg("repDelay").toInt() : 0;
  r.accelRpm = argPresent("acceleration") ? s_server.arg("acceleration").toFloat() : DEFAULT_ACCELERATION_RPM_S;
  r.decelRpm = argPresent("deceleration") ? s_server.arg("deceleration").toFloat() : DEFAULT_DECELERATION_RPM_S;
  return r;
}

static void handleRoot() {
  String html = WEB_PAGE;
  html.replace("{{MAX_SPEED}}", String(MAX_SPEED_RPM, 1));
  html.replace("{{MAX_ACCEL}}", String(MAX_ACCELERATION_RPM_S, 1));
  html.replace("{{DEF_ACCEL}}", String(DEFAULT_ACCELERATION_RPM_S, 1));
  html.replace("{{DEF_DECEL}}", String(DEFAULT_DECELERATION_RPM_S, 1));
  s_server.send(200, "text/html", html);
}

// v1-compatible: blocks until the sequence finishes, then returns the HTML report.
static void handleLegacyRotate() {
  if (!(s_server.hasArg("angle") && s_server.hasArg("time") && s_server.hasArg("direction"))) {
    s_server.send(400, "text/plain", "Missing parameters.");
    return;
  }
  RotateRequest req = requestFromArgs();
  uint32_t runId = 0;
  const char* error = "";
  switch (motion::submit(req, &runId, &error)) {
    case SubmitResult::Ok: break;
    case SubmitResult::NoMovement:
      // v1 did nothing and returned the previous report.
      s_server.send(200, "text/html", api::reportHtml(motion::status()));
      return;
    case SubmitResult::Busy:
      s_server.send(409, "text/plain", "Busy: a rotation is already running.");
      return;
    case SubmitResult::Locked:
      s_server.send(503, "text/plain", "Firmware update in progress.");
      return;
    case SubmitResult::Invalid:
      s_server.send(400, "text/plain", error);
      return;
  }

  // Generous bound: every rep can take 2x its time (v1 safety), plus stopping.
  uint32_t deadline = millis() + maxSequenceMs(req) + 15000;
  MotionStatus s;
  for (;;) {
    s = motion::status();
    bool finished = s.runId == runId && (s.state == MotionState::Done || s.state == MotionState::Stopped);
    if (finished || s.runId != runId || (int32_t)(millis() - deadline) > 0) break;
    netYield();
  }
  s_server.send(200, "text/html", api::reportHtml(s));
}

static void handleApiRotate() {
  JsonDocument cmd, reply;
  if (!(s_server.hasArg("angle") && s_server.hasArg("time"))) {
    reply["ok"] = false;
    reply["error"] = "invalid";
    reply["message"] = "angle and time are required";
    sendJson(400, reply);
    return;
  }
  RotateRequest r = requestFromArgs();
  cmd["cmd"] = "rotate";
  cmd["angle"] = r.angle;
  cmd["time"] = r.timeMs;
  cmd["direction"] = r.reverse ? "reverse" : "forward";
  cmd["repetitions"] = r.repetitions;
  cmd["repDelay"] = r.repDelayMs;
  cmd["acceleration"] = r.accelRpm;
  cmd["deceleration"] = r.decelRpm;
  api::handleCommand(cmd, reply.to<JsonObject>());
  sendJson(200, reply);
}

static void handleApiStop() {
  motion::requestStop();
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleApiStatus() {
  JsonDocument doc;
  api::stateJson(doc.to<JsonObject>());
  sendJson(200, doc);
}

static void handleApiReport() {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/html", api::reportHtml(motion::status()));
}

static void handleApiInfo() {
  JsonDocument doc;
  api::infoJson(doc.to<JsonObject>());
  sendJson(200, doc);
}

static void handleApiScanGet() {
  JsonDocument doc;
  doc["inProgress"] = wifimgr::scanInProgress();
  doc["scan"] = wifimgr::scanId();
  JsonArray arr = doc["networks"].to<JsonArray>();
  wifimgr::Network nets[WIFI_MAX_SCAN_RESULTS];
  int n = wifimgr::scanResults(nets, WIFI_MAX_SCAN_RESULTS);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = nets[i].ssid;
    o["rssi"] = nets[i].rssi;
    o["channel"] = nets[i].channel;
    o["secure"] = nets[i].secure;
  }
  sendJson(200, doc);
}

static void handleApiScanPost() {
  wifimgr::requestScan();
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleApiWifi() {
  JsonDocument cmd, reply;
  cmd["cmd"] = "wifi_set";
  cmd["ssid"] = s_server.arg("ssid");
  cmd["pass"] = s_server.arg("pass");
  api::handleCommand(cmd, reply.to<JsonObject>());
  sendJson(200, reply);
}

// Captive portal: phones probe a known URL when joining the hotspot; redirecting
// it makes them pop up the setup page and keep the (internet-less) connection.
static void handleNotFound() {
  wifimgr::Info w = wifimgr::info();
  String host = s_server.hostHeader();
  if (w.apActive && host != w.apIp && host != w.ip && host != HOSTNAME ".local") {
    s_server.sendHeader("Location", String("http://") + w.apIp + "/", true);
    s_server.send(302, "text/plain", "");
    return;
  }
  s_server.send(404, "text/plain", "Not found");
}

#ifdef TT_TEST_HOOKS
// Freezes the network task to prove the software watchdog recovers the device.
static void handleDebugStall() {
  s_server.send(200, "text/plain", "stalling network task");
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif

namespace web {

void begin() {
  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/rotate", handleLegacyRotate);
  s_server.on("/api/rotate", handleApiRotate);
  s_server.on("/api/stop", handleApiStop);
  s_server.on("/api/status", HTTP_GET, handleApiStatus);
  s_server.on("/api/report", HTTP_GET, handleApiReport);
  s_server.on("/api/info", HTTP_GET, handleApiInfo);
  s_server.on("/api/scan", HTTP_GET, handleApiScanGet);
  s_server.on("/api/scan", HTTP_POST, handleApiScanPost);
  s_server.on("/api/wifi", HTTP_POST, handleApiWifi);
  ota::begin(s_server);
#ifdef TT_TEST_HOOKS
  s_server.on("/debug/stall", HTTP_POST, handleDebugStall);
#endif
  s_server.onNotFound(handleNotFound);
  s_server.begin();
}

void tick() {
  s_server.handleClient();
}

WebServer& server() { return s_server; }

}  // namespace web
