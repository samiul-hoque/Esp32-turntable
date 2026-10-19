#include <Arduino.h>
#include <NimBLEDevice.h>
#include <deque>
#include <string>
#include "api.h"
#include "ble_service.h"
#include "config.h"
#include "device.h"

#define LOGB(fmt, ...) Serial.printf("[ble] " fmt "\n", ##__VA_ARGS__)

static const size_t CMD_MAX = 512;
static const size_t OUTBOX_MAX = 64;

struct CmdMsg {
  char data[CMD_MAX];
};

static NimBLEServer* s_server = nullptr;
static NimBLECharacteristic* s_stateChr = nullptr;
static NimBLECharacteristic* s_eventsChr = nullptr;
static QueueHandle_t s_cmdQueue = nullptr;

// Outgoing newline-delimited JSON, sent in MTU-sized chunks (network task only).
static std::deque<std::string> s_outbox;
static size_t s_outOffset = 0;
static uint32_t s_lastNotify = 0;

// Change tracking for events (network task only).
static uint32_t s_lastRun = UINT32_MAX;
static MotionState s_lastMotionState = MotionState::Idle;
static int s_lastRep = -1;
static uint32_t s_lastProgress = 0;
static uint32_t s_lastWifiChange = UINT32_MAX;
static uint32_t s_lastScanId = 0;
static uint32_t s_lastStateUpdate = 0;
static size_t s_lastConnCount = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
    // 30-50 ms interval, 5 s supervision timeout: tolerant of Wi-Fi coexistence gaps.
    server->updateConnParams(desc->conn_handle, 24, 40, 0, 500);
    // Keep advertising so a second phone/laptop can still connect.
    // (advertiseOnDisconnect restarts it if the connection limit stopped it.)
    if (server->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) NimBLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, ble_gap_conn_desc* desc) override {
    NimBLEAttValue v = chr->getValue();
    CmdMsg msg;
    size_t n = v.length() < CMD_MAX - 1 ? v.length() : CMD_MAX - 1;
    memcpy(msg.data, v.data(), n);
    msg.data[n] = 0;
    // Stop must never wait behind anything else (e.g. a long HTTP request).
    JsonDocument doc;
    if (!deserializeJson(doc, msg.data) && doc["cmd"] == "stop") motion::requestStop();
    if (xQueueSend(s_cmdQueue, &msg, 0) != pdTRUE) LOGB("command queue full, dropped");
  }
};

static void enqueue(JsonDocument& doc) {
  std::string line;
  serializeJson(doc, line);
  line += '\n';
  if (s_outbox.size() >= OUTBOX_MAX) {
    s_outbox.pop_back();  // drop the newest queued (never the one mid-send)
  }
  s_outbox.push_back(std::move(line));
}

static size_t chunkSize() {
  uint16_t mtu = 0;
  for (uint16_t conn : s_server->getPeerDevices()) {
    uint16_t m = s_server->getPeerMTU(conn);
    if (m && (mtu == 0 || m < mtu)) mtu = m;
  }
  if (mtu < 23) mtu = 23;
  size_t size = mtu - 3;
  return size > 244 ? 244 : size;
}

static void sendNextChunk(uint32_t now) {
  if (s_outbox.empty() || now - s_lastNotify < BLE_NOTIFY_SPACING_MS) return;
  const std::string& msg = s_outbox.front();
  size_t len = msg.size() - s_outOffset;
  size_t max = chunkSize();
  if (len > max) len = max;
  s_eventsChr->notify(reinterpret_cast<const uint8_t*>(msg.data()) + s_outOffset, len);
  s_lastNotify = now;
  s_outOffset += len;
  if (s_outOffset >= msg.size()) {
    s_outbox.pop_front();
    s_outOffset = 0;
  }
}

static void processCommands() {
  CmdMsg msg;
  while (xQueueReceive(s_cmdQueue, &msg, 0) == pdTRUE) {
    JsonDocument cmd, reply;
    DeserializationError err = deserializeJson(cmd, msg.data);
    if (err) {
      reply["type"] = "reply";
      reply["ok"] = false;
      reply["error"] = "bad_json";
      reply["message"] = err.c_str();
    } else {
      api::handleCommand(cmd, reply.to<JsonObject>());
    }
    enqueue(reply);
  }
}

static void collectEvents(uint32_t now) {
  MotionStatus m = motion::status();
  bool active = m.state == MotionState::Moving || m.state == MotionState::RepDelay;
  if (m.runId != s_lastRun || m.state != s_lastMotionState || m.currentRep != s_lastRep ||
      (active && now - s_lastProgress >= BLE_PROGRESS_INTERVAL_MS)) {
    JsonDocument doc;
    doc["type"] = "motion";
    api::motionJson(doc.as<JsonObject>(), m);
    enqueue(doc);
    s_lastRun = m.runId;
    s_lastMotionState = m.state;
    s_lastRep = m.currentRep;
    s_lastProgress = now;
  }

  uint32_t wifiChange = wifimgr::changeCounter();
  if (wifiChange != s_lastWifiChange) {
    s_lastWifiChange = wifiChange;
    JsonDocument doc;
    doc["type"] = "wifi";
    api::wifiJson(doc["wifi"].to<JsonObject>(), wifimgr::info());
    api::testJson(doc["test"].to<JsonObject>(), wifimgr::lastTest());
    enqueue(doc);
  }

  uint32_t scanId = wifimgr::scanId();
  if (scanId != s_lastScanId) {
    s_lastScanId = scanId;
    wifimgr::Network nets[WIFI_MAX_SCAN_RESULTS];
    int n = wifimgr::scanResults(nets, WIFI_MAX_SCAN_RESULTS);
    for (int i = 0; i < n; i++) {
      JsonDocument doc;
      doc["type"] = "net";
      doc["ssid"] = nets[i].ssid;
      doc["rssi"] = nets[i].rssi;
      doc["channel"] = nets[i].channel;
      doc["secure"] = nets[i].secure;
      enqueue(doc);
    }
    JsonDocument done;
    done["type"] = "scan_done";
    done["scan"] = scanId;
    done["count"] = n;
    enqueue(done);
  }
}

static void updateStateValue(uint32_t now) {
  if (now - s_lastStateUpdate < 200) return;
  s_lastStateUpdate = now;
  JsonDocument doc;
  api::stateJson(doc.to<JsonObject>());
  std::string out;
  serializeJson(doc, out);
  s_stateChr->setValue(out);
}

namespace ble {

void begin() {
  s_cmdQueue = xQueueCreate(4, sizeof(CmdMsg));

  NimBLEDevice::init(device::name());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new ServerCallbacks());
  s_server->advertiseOnDisconnect(true);

  NimBLEService* svc = s_server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic* cmd = svc->createCharacteristic(
      BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE, CMD_MAX);
  cmd->setCallbacks(new CommandCallbacks());
  s_stateChr = svc->createCharacteristic(BLE_STATE_UUID, NIMBLE_PROPERTY::READ, 1024);
  s_eventsChr = svc->createCharacteristic(BLE_EVENTS_UUID, NIMBLE_PROPERTY::NOTIFY);
  svc->start();

  // 128-bit service UUID in the advertisement (what the app filters on);
  // the name goes in the scan response because both don't fit in 31 bytes.
  NimBLEAdvertisementData adv;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  adv.setCompleteServices(NimBLEUUID(BLE_SERVICE_UUID));
  NimBLEAdvertisementData scanResp;
  scanResp.setName(device::name());
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(adv);
  advertising->setScanResponseData(scanResp);
  advertising->start();

  LOGB("advertising as %s", device::name());
}

void tick() {
  if (!s_server) return;
  uint32_t now = millis();
  processCommands();
  updateStateValue(now);  // keep the readable state fresh so the first read after connecting is current

  size_t conns = s_server->getConnectedCount();
  if (conns != s_lastConnCount) {
    LOGB("%u connected", (unsigned)conns);
    if (conns > s_lastConnCount) {
      // Fresh subscriber: resend current state as events.
      s_lastRun = UINT32_MAX;
      s_lastWifiChange = UINT32_MAX;
    }
    s_lastConnCount = conns;
  }
  if (conns == 0) {
    s_outbox.clear();
    s_outOffset = 0;
    return;
  }

  collectEvents(now);
  sendNextChunk(now);
}

}  // namespace ble
