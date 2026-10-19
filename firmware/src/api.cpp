#include "api.h"
#include "config.h"
#include "device.h"

static const char* LOCKED_MSG =
    "Wi-Fi settings are locked while connected. Power-cycle the turntable, then change Wi-Fi within 10 minutes.";

static void setError(JsonObject reply, const char* code, const char* message) {
  reply["ok"] = false;
  reply["error"] = code;
  reply["message"] = message;
}

static void handleRotate(JsonVariantConst cmd, JsonObject reply) {
  if (!cmd["angle"].is<int>() || !cmd["time"].is<int>()) {
    setError(reply, "invalid", "angle and time are required");
    return;
  }
  RotateRequest r;
  r.angle = cmd["angle"].as<int>();
  r.timeMs = cmd["time"].as<int>();
  r.reverse = cmd["direction"] == "reverse" || cmd["reverse"] == true;
  r.repetitions = cmd["repetitions"] | 1;
  r.repDelayMs = cmd["repDelay"] | 0;
  r.accelRpm = cmd["acceleration"] | DEFAULT_ACCELERATION_RPM_S;
  r.decelRpm = cmd["deceleration"] | DEFAULT_DECELERATION_RPM_S;

  uint32_t runId = 0;
  const char* error = "";
  SubmitResult res = motion::submit(r, &runId, &error);
  switch (res) {
    case SubmitResult::Ok:
      reply["ok"] = true;
      reply["run"] = runId;
      break;
    case SubmitResult::Busy: setError(reply, "busy", "A rotation is already running"); break;
    case SubmitResult::Invalid: setError(reply, "invalid", error); break;
    case SubmitResult::NoMovement: setError(reply, "no_movement", "Angle is too small to move"); break;
    case SubmitResult::Locked: setError(reply, "locked", "Firmware update in progress"); break;
  }
}

static void wifiReply(JsonObject reply, wifimgr::Request r, const char* error) {
  switch (r) {
    case wifimgr::Request::Accepted: reply["ok"] = true; break;
    case wifimgr::Request::Locked: setError(reply, "locked", LOCKED_MSG); break;
    case wifimgr::Request::Busy: setError(reply, "busy", "A Wi-Fi change is already in progress"); break;
    case wifimgr::Request::Invalid: setError(reply, "invalid", error); break;
  }
}

namespace api {

void motionJson(JsonObject o, const MotionStatus& s) {
  o["state"] = motion::stateName(s.state);
  o["run"] = s.runId;
  if (s.state == MotionState::Idle) return;
  o["rep"] = s.currentRep;
  o["reps"] = s.req.repetitions;
  o["angle"] = s.req.angle;
  o["direction"] = s.req.reverse ? "reverse" : "forward";
  o["time"] = s.req.timeMs;
  o["repDelay"] = s.req.repDelayMs;
  o["elapsed"] = s.elapsedMs;
  o["speedRpm"] = serialized(String(s.plan.speedRpm, 2));
  o["accel"] = serialized(String(s.plan.accelRpm, 1));
  o["decel"] = serialized(String(s.plan.decelRpm, 1));
  o["steps"] = s.totalStepsMoved;
  o["actualAngle"] = serialized(String(stepsToDegrees(s.totalStepsMoved), 1));
  if (s.plan.warnings[0]) o["warnings"] = s.plan.warnings;
}

void wifiJson(JsonObject o, const wifimgr::Info& i) {
  o["state"] = wifimgr::stateName(i.state);
  o["configured"] = i.hasCredentials;
  o["ssid"] = i.ssid;
  if (i.ip[0]) {
    o["ip"] = i.ip;
    o["rssi"] = i.rssi;
    o["channel"] = i.channel;
    o["host"] = HOSTNAME ".local";
  }
  if (i.lastReason || i.state == wifimgr::StaState::WaitingRetry) {
    o["reason"] = i.lastReason;
    o["reasonText"] = wifimgr::reasonText(i.lastReason);
  }
  if (i.state == wifimgr::StaState::WaitingRetry) o["retryIn"] = i.nextRetrySec;
  o["hotspot"] = i.apActive;
  if (i.apActive) {
    o["hotspotSsid"] = i.apSsid;
    o["hotspotIp"] = i.apIp;
    o["hotspotClients"] = i.apClients;
  }
  o["setupAllowed"] = i.setupAllowed;
}

void testJson(JsonObject o, const wifimgr::TestResult& t) {
  o["id"] = t.id;
  o["state"] = wifimgr::testStateName(t.state);
  if (t.state == wifimgr::TestState::None) return;
  o["ssid"] = t.ssid;
  if (t.state == wifimgr::TestState::Ok) o["ip"] = t.ip;
  if (t.state == wifimgr::TestState::Failed) {
    o["reason"] = t.reason;
    o["reasonText"] = wifimgr::reasonText(t.reason);
  }
}

void infoJson(JsonObject o) {
  o["fw"] = FW_VERSION;
  o["proto"] = PROTO_VERSION;
  o["device"] = device::name();
  o["uptime"] = device::uptimeSec();
  o["resetReason"] = device::resetReason();
  o["freeHeap"] = ESP.getFreeHeap();
  o["minFreeHeap"] = ESP.getMinFreeHeap();
  JsonObject lim = o["limits"].to<JsonObject>();
  lim["maxSpeedRpm"] = MAX_SPEED_RPM;
  lim["maxAccelRpmS"] = MAX_ACCELERATION_RPM_S;
  lim["defaultAccelRpmS"] = DEFAULT_ACCELERATION_RPM_S;
  lim["defaultDecelRpmS"] = DEFAULT_DECELERATION_RPM_S;
  wifiJson(o["wifi"].to<JsonObject>(), wifimgr::info());
}

void stateJson(JsonObject o) {
  o["proto"] = PROTO_VERSION;
  motionJson(o["motion"].to<JsonObject>(), motion::status());
  wifiJson(o["wifi"].to<JsonObject>(), wifimgr::info());
  testJson(o["test"].to<JsonObject>(), wifimgr::lastTest());
}

void handleCommand(JsonVariantConst cmd, JsonObject reply) {
  reply["type"] = "reply";
  if (!cmd["id"].isNull()) reply["id"] = cmd["id"];
  const char* name = cmd["cmd"] | "";
  reply["cmd"] = name;

  if (strcmp(name, "rotate") == 0) {
    handleRotate(cmd, reply);
  } else if (strcmp(name, "stop") == 0) {
    motion::requestStop();
    reply["ok"] = true;
  } else if (strcmp(name, "info") == 0) {
    reply["ok"] = true;
    infoJson(reply["info"].to<JsonObject>());
  } else if (strcmp(name, "state") == 0) {
    reply["ok"] = true;
    stateJson(reply["state"].to<JsonObject>());
  } else if (strcmp(name, "wifi_scan") == 0) {
    wifimgr::requestScan();
    reply["ok"] = true;
  } else if (strcmp(name, "wifi_set") == 0) {
    const char* error = "";
    wifimgr::Request r = wifimgr::setCredentials(cmd["ssid"] | "", cmd["pass"] | "", &error);
    wifiReply(reply, r, error);
    if (r == wifimgr::Request::Accepted) reply["test"] = wifimgr::lastTest().id;
  } else if (strcmp(name, "wifi_forget") == 0) {
    wifiReply(reply, wifimgr::forget(), "");
  } else {
    setError(reply, "unknown_command", "Unknown command");
  }
}

String reportHtml(const MotionStatus& s) {
  if (s.state == MotionState::Idle) return "";
  const RotateRequest& r = s.req;
  const MotionPlan& p = s.plan;
  float expectedTotalAngle = (float)r.angle * r.repetitions;
  float actualTotalAngle = stepsToDegrees(s.totalStepsMoved);
  String directionStr = r.reverse ? "Reverse" : "Forward";
  unsigned long totalDuration = s.elapsedMs;
  String totalTimeStr = totalDuration > 10000 ? String(totalDuration / 1000.0, 1) + "s" : String(totalDuration) + "ms";

  String out;
  out.reserve(1200);
  if (s.state == MotionState::Stopped) {
    out += "<b>Sequence Stopped</b><br><br>";
  } else {
    out += "<b>Sequence Complete!</b><br><br>";
  }
  if (p.warnings[0]) {
    out += String("<b style='color: orange;'>WARNING: ") + p.warnings + String("</b><br><br>");
  }
  out += "<b>Movement:</b><br>";
  out += String("• ") + String(r.repetitions) + " repetitions<br>";
  out += String("• ") + String(r.angle) + "° per rep (" + directionStr + ")<br>";
  out += "• Total angle: " + String(expectedTotalAngle, 1) + "°<br><br>";
  out += "<b>Timing:</b><br>";
  out += "• Movement time per rep: " + String(r.timeMs) + "ms<br>";
  out += "• Delay between reps: " + String(r.repDelayMs) + "ms<br>";
  out += "• Total sequence time: " + totalTimeStr + "<br><br>";
  out += "<b>Motion Profile:</b><br>";
  out += "• Target speed: " + String(p.speedRpm, 2) + " RPM<br>";
  out += "• Acceleration: " + String(p.accelRpm, 1) + " RPM/s<br>";
  out += "• Deceleration: " + String(p.decelRpm, 1) + " RPM/s<br><br>";
  out += "<b>Technical:</b><br>";
  out += "• Steps moved: " + String(s.totalStepsMoved) + "<br>";
  out += "• Actual angle: " + String(actualTotalAngle, 1) + "°";
  return out;
}

}  // namespace api
