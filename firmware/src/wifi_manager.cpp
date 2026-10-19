#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <atomic>
#include "config.h"
#include "device.h"
#include "wifi_manager.h"

#define LOGW(fmt, ...) Serial.printf("[wifi] " fmt "\n", ##__VA_ARGS__)

using namespace wifimgr;

static Preferences s_prefs;
static DNSServer s_dns;

// Stored (committed) credentials.
static char s_ssid[33] = "";
static char s_pass[65] = "";
static bool s_hasCreds = false;

static StaState s_state = StaState::NoCredentials;
static uint32_t s_attemptStart = 0;
static uint32_t s_attemptTimeout = 0;
static uint32_t s_attemptDiscBase = 0;
static uint32_t s_nextRetryAt = 0;
static uint32_t s_offlineSince = 0;
static uint32_t s_connectedSince = 0;
static uint8_t s_failCount = 0;      // consecutive failed attempts
static uint8_t s_authFails = 0;      // consecutive wrong-password-looking failures
static uint8_t s_lastReason = 0;

static bool s_apActive = false;

// Credential test.
static bool s_testPending = false;
static char s_testSsid[33];
static char s_testPass[65];
static TestResult s_test = {};
static uint32_t s_testCounter = 0;

// Scan.
static bool s_scanRequested = false;
static bool s_scanRunning = false;
static uint32_t s_scanStarted = 0;
static uint32_t s_scanId = 0;
static Network s_nets[WIFI_MAX_SCAN_RESULTS];
static int s_netCount = 0;

static uint32_t s_change = 0;

// Written by the Wi-Fi event task, read by tick().
static std::atomic<uint32_t> ev_discCount{0};
static std::atomic<uint8_t> ev_lastReason{0};
static std::atomic<uint32_t> ev_lastDiscAt{0};

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    if (reason == WIFI_REASON_ASSOC_LEAVE) return;  // our own disconnect()
    ev_lastReason = reason;
    ev_lastDiscAt = millis();
    ev_discCount++;
  }
}

static bool staConnected() {
  return WiFi.status() == WL_CONNECTED;
}

static void applyCountry() {
  wifi_country_t country = {};
  strncpy(country.cc, WIFI_COUNTRY, sizeof(country.cc));
  country.schan = WIFI_CHANNEL_FIRST;
  country.nchan = WIFI_CHANNEL_COUNT;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_err_t err = esp_wifi_set_country(&country);
  if (err != ESP_OK) LOGW("set_country failed: %d", err);
}

static void loadCredentials() {
  s_prefs.begin("wifi", false);
  // Seed once from secrets.h so a device upgraded from v1 reconnects with no user action.
  // The "seeded" flag stops a later "forget" from being undone on the next boot.
  if (!s_prefs.getBool("seeded", false)) {
    const char* seed = device::seedSsid();
    if (seed[0]) {
      s_prefs.putString("ssid", seed);
      s_prefs.putString("pass", device::seedPassword());
      LOGW("seeded credentials from secrets.h (%s)", seed);
    }
    s_prefs.putBool("seeded", true);
  }
  String ssid = s_prefs.getString("ssid", "");
  String pass = s_prefs.getString("pass", "");
  strlcpy(s_ssid, ssid.c_str(), sizeof(s_ssid));
  strlcpy(s_pass, pass.c_str(), sizeof(s_pass));
  s_hasCreds = s_ssid[0] != 0;
}

static void startAp() {
  if (s_apActive) return;
  WiFi.mode(WIFI_AP_STA);
  const char* pass = device::apPassword();
  bool ok = strlen(pass) >= 8 ? WiFi.softAP(device::name(), pass) : WiFi.softAP(device::name());
  if (!ok) {
    LOGW("hotspot failed to start");
    return;
  }
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name resolves to us
  s_apActive = true;
  s_scanRequested = true;  // have a network list ready before anyone joins
  LOGW("hotspot %s up at %s", device::name(), WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!s_apActive) return;
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  s_apActive = false;
  LOGW("hotspot stopped");
}

static void startAttempt(const char* ssid, const char* pass, StaState state, uint32_t timeoutMs) {
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  s_state = state;
  s_attemptStart = millis();
  s_attemptTimeout = timeoutMs;
  s_attemptDiscBase = ev_discCount;
  LOGW("%s to \"%s\"", state == StaState::Testing ? "testing" : "connecting", ssid);
}

static void restartMdns() {
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  } else {
    LOGW("mDNS failed to start");
  }
}

static bool isAuthReason(uint8_t r) {
  return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_EXPIRE;
}

static void scheduleRetry(uint32_t now) {
  uint32_t delayMs;
  if (s_authFails >= WIFI_AUTH_FAIL_LIMIT) {
    delayMs = WIFI_AUTH_FAIL_BACKOFF_MS;
  } else {
    size_t n = sizeof(WIFI_RETRY_BACKOFF_MS) / sizeof(WIFI_RETRY_BACKOFF_MS[0]);
    delayMs = WIFI_RETRY_BACKOFF_MS[s_failCount < n ? s_failCount : n - 1];
  }
  // A retry scan hops channels and knocks phones off the hotspot, so retry
  // rarely while someone is using it (they're probably setting up Wi-Fi).
  if (s_apActive && WiFi.softAPgetStationNum() > 0 && delayMs < WIFI_RETRY_WITH_AP_CLIENT_MS) {
    delayMs = WIFI_RETRY_WITH_AP_CLIENT_MS;
  }
  s_nextRetryAt = now + delayMs;
  s_state = StaState::WaitingRetry;
  LOGW("retry in %lus", (unsigned long)(delayMs / 1000));
}

static void onConnected(uint32_t now) {
  s_state = StaState::Connected;
  s_connectedSince = now;
  s_failCount = 0;
  s_authFails = 0;
  restartMdns();
  LOGW("connected: %s  ip=%s  rssi=%d  ch=%d", WiFi.SSID().c_str(),
       WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
}

static void tickAttempt(uint32_t now) {
  if (staConnected()) {
    if (s_state == StaState::Testing) {
      s_prefs.putString("ssid", s_testSsid);
      s_prefs.putString("pass", s_testPass);
      strlcpy(s_ssid, s_testSsid, sizeof(s_ssid));
      strlcpy(s_pass, s_testPass, sizeof(s_pass));
      s_hasCreds = true;
      s_test.state = TestState::Ok;
      strlcpy(s_test.ip, WiFi.localIP().toString().c_str(), sizeof(s_test.ip));
      LOGW("new credentials saved");
    }
    s_lastReason = 0;
    onConnected(now);
    return;
  }

  uint32_t discs = ev_discCount - s_attemptDiscBase;
  bool settled = discs > 0 && now - ev_lastDiscAt > WIFI_ATTEMPT_SETTLE_MS;
  bool timedOut = now - s_attemptStart > s_attemptTimeout;
  if (!settled && !timedOut) return;

  uint8_t reason = discs > 0 ? ev_lastReason.load() : 0;
  s_lastReason = reason;
  WiFi.disconnect();
  LOGW("attempt failed: %u %s", reason, reasonText(reason));

  if (s_state == StaState::Testing) {
    s_test.state = TestState::Failed;
    s_test.reason = reason;
    // Keep the previous network: reconnect to it right away.
    if (s_hasCreds) {
      s_failCount = 0;
      s_nextRetryAt = now;
      s_state = StaState::WaitingRetry;
    } else {
      s_state = StaState::NoCredentials;
    }
    return;
  }

  s_authFails = isAuthReason(reason) ? s_authFails + 1 : 0;
  scheduleRetry(now);
  if (s_failCount < 255) s_failCount++;
}

static void tickScan(uint32_t now) {
  if (s_scanRunning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - s_scanStarted < 15000) return;
    s_netCount = 0;
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;  // hidden networks: use "enter manually"
      int8_t rssi = WiFi.RSSI(i);
      // De-duplicate mesh/multi-AP networks, keeping the strongest.
      int found = -1;
      for (int j = 0; j < s_netCount; j++) {
        if (strcmp(s_nets[j].ssid, ssid.c_str()) == 0) { found = j; break; }
      }
      if (found >= 0) {
        if (rssi > s_nets[found].rssi) {
          s_nets[found].rssi = rssi;
          s_nets[found].channel = WiFi.channel(i);
        }
        continue;
      }
      if (s_netCount >= WIFI_MAX_SCAN_RESULTS) {
        // Replace the weakest entry if this one is stronger.
        int weakest = 0;
        for (int j = 1; j < s_netCount; j++) if (s_nets[j].rssi < s_nets[weakest].rssi) weakest = j;
        if (rssi <= s_nets[weakest].rssi) continue;
        found = weakest;
      } else {
        found = s_netCount++;
      }
      Network& net = s_nets[found];
      strlcpy(net.ssid, ssid.c_str(), sizeof(net.ssid));
      net.rssi = rssi;
      net.channel = WiFi.channel(i);
      net.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    // Strongest first.
    for (int i = 1; i < s_netCount; i++) {
      Network key = s_nets[i];
      int j = i - 1;
      while (j >= 0 && s_nets[j].rssi < key.rssi) { s_nets[j + 1] = s_nets[j]; j--; }
      s_nets[j + 1] = key;
    }
    WiFi.scanDelete();
    s_scanRunning = false;
    s_scanId++;
    s_change++;
    LOGW("scan done: %d networks", s_netCount);
    return;
  }

  if (!s_scanRequested) return;
  // A background reconnect attempt can be interrupted for a scan the user is
  // waiting on; a credential test cannot.
  if (s_state == StaState::Testing || s_testPending) return;
  if (s_state == StaState::Connecting) {
    WiFi.disconnect();
    s_nextRetryAt = now + WIFI_RETRY_BACKOFF_MS[0];
    s_state = StaState::WaitingRetry;
  }
  s_scanRequested = false;
  int r = WiFi.scanNetworks(true /*async*/, false /*hidden*/, false /*passive*/, 300 /*ms per channel*/);
  if (r == WIFI_SCAN_FAILED) {
    LOGW("scan failed to start");
    s_scanId++;
    s_change++;
    return;
  }
  s_scanRunning = true;
  s_scanStarted = now;
}

namespace wifimgr {

void begin() {
  loadCredentials();
  WiFi.persistent(false);       // our credentials live in our own NVS namespace only
  WiFi.setAutoReconnect(false); // tick() owns reconnection
  WiFi.setHostname(HOSTNAME);   // must precede mode()
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  applyCountry();
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);          // see every AP, not just the first
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);      // join the strongest (mesh / multi-AP)
  WiFi.setMinSecurity(WIFI_AUTH_WEP);                 // accept WPA/TKIP and mixed-mode routers
  // Modem sleep stays enabled: it is mandatory while BLE is running.

  s_offlineSince = millis();
  if (s_hasCreds) {
    startAttempt(s_ssid, s_pass, StaState::Connecting, WIFI_CONNECT_TIMEOUT_MS);
  } else {
    LOGW("no credentials stored");
    s_state = StaState::NoCredentials;
    startAp();
  }
}

void tick() {
  uint32_t now = millis();
  static StaState lastState = s_state;
  static bool lastAp = s_apActive;
  static uint8_t lastClients = 0;

  switch (s_state) {
    case StaState::Connecting:
    case StaState::Testing:
      tickAttempt(now);
      break;
    case StaState::Connected:
      if (!staConnected()) {
        s_lastReason = ev_lastReason;
        s_offlineSince = now;
        LOGW("connection lost: %u %s", s_lastReason, reasonText(s_lastReason));
        MDNS.end();
        s_failCount = 0;
        s_nextRetryAt = now;  // first retry immediately
        s_state = StaState::WaitingRetry;
      }
      break;
    case StaState::WaitingRetry:
      if (!s_scanRunning && !s_testPending && (int32_t)(now - s_nextRetryAt) >= 0) {
        startAttempt(s_ssid, s_pass, StaState::Connecting, WIFI_CONNECT_TIMEOUT_MS);
      }
      break;
    case StaState::NoCredentials:
      break;
  }

  if (s_testPending && !s_scanRunning && s_state != StaState::Testing) {
    s_testPending = false;
    startAttempt(s_testSsid, s_testPass, StaState::Testing, WIFI_TEST_TIMEOUT_MS);
  }

  tickScan(now);

  // Hotspot: up when offline for a while (or nothing configured), down once
  // the station has been connected for a bit and nobody needs the hotspot.
  if (s_state != StaState::Connected) {
    if (!s_apActive && (!s_hasCreds || now - s_offlineSince > WIFI_AP_FALLBACK_MS)) startAp();
  } else if (s_apActive) {
    uint32_t up = now - s_connectedSince;
    if (up > WIFI_AP_LINGER_MS && (WiFi.softAPgetStationNum() == 0 || up > 3 * WIFI_AP_LINGER_MS)) stopAp();
  }
  if (s_apActive) s_dns.processNextRequest();

  uint8_t clients = s_apActive ? WiFi.softAPgetStationNum() : 0;
  if (s_state != lastState || s_apActive != lastAp || clients != lastClients) {
    lastState = s_state;
    lastAp = s_apActive;
    lastClients = clients;
    s_change++;
  }
}

Info info() {
  Info i = {};
  i.state = s_state;
  i.hasCredentials = s_hasCreds;
  strlcpy(i.ssid, s_ssid, sizeof(i.ssid));
  if (s_state == StaState::Connected) {
    strlcpy(i.ip, WiFi.localIP().toString().c_str(), sizeof(i.ip));
    i.rssi = WiFi.RSSI();
    i.channel = WiFi.channel();
  }
  i.lastReason = s_lastReason;
  if (s_state == StaState::WaitingRetry) {
    int32_t left = (int32_t)(s_nextRetryAt - millis());
    i.nextRetrySec = left > 0 ? left / 1000 : 0;
  }
  i.apActive = s_apActive;
  if (s_apActive) {
    strlcpy(i.apSsid, device::name(), sizeof(i.apSsid));
    strlcpy(i.apIp, WiFi.softAPIP().toString().c_str(), sizeof(i.apIp));
    i.apClients = WiFi.softAPgetStationNum();
  }
  i.setupAllowed = setupAllowed();
  return i;
}

TestResult lastTest() { return s_test; }

uint32_t changeCounter() { return s_change; }

bool setupAllowed() {
  return millis() < SETUP_WINDOW_MS || s_state != StaState::Connected;
}

Request setCredentials(const char* ssid, const char* pass, const char** error) {
  size_t sl = ssid ? strlen(ssid) : 0;
  size_t pl = pass ? strlen(pass) : 0;
  if (sl < 1 || sl > 32) { if (error) *error = "Network name must be 1-32 characters"; return Request::Invalid; }
  if (pl > 64 || (pl > 0 && pl < 5)) { if (error) *error = "Password must be empty (open network) or 5-64 characters"; return Request::Invalid; }
  if (!setupAllowed()) return Request::Locked;
  if (s_state == StaState::Testing || s_testPending) return Request::Busy;

  strlcpy(s_testSsid, ssid, sizeof(s_testSsid));
  strlcpy(s_testPass, pass ? pass : "", sizeof(s_testPass));
  s_test = {};
  s_test.id = ++s_testCounter;
  s_test.state = TestState::Pending;
  strlcpy(s_test.ssid, ssid, sizeof(s_test.ssid));
  s_testPending = true;
  s_change++;
  return Request::Accepted;
}

Request forget() {
  if (!setupAllowed()) return Request::Locked;
  if (s_state == StaState::Testing || s_testPending) return Request::Busy;
  s_prefs.remove("ssid");
  s_prefs.remove("pass");
  s_ssid[0] = s_pass[0] = 0;
  s_hasCreds = false;
  WiFi.disconnect();
  MDNS.end();
  s_state = StaState::NoCredentials;
  s_offlineSince = millis();
  startAp();
  s_change++;
  LOGW("credentials forgotten");
  return Request::Accepted;
}

void requestScan() { s_scanRequested = true; }
bool scanInProgress() { return s_scanRunning || s_scanRequested; }
uint32_t scanId() { return s_scanId; }

int scanResults(Network* out, int max) {
  int n = s_netCount < max ? s_netCount : max;
  for (int i = 0; i < n; i++) out[i] = s_nets[i];
  return n;
}

const char* reasonText(uint8_t r) {
  switch (r) {
    case 0: return "No response from the network (timed out)";
    case WIFI_REASON_NO_AP_FOUND: return "Network not found. Check the name and that the router has 2.4 GHz enabled";
    case WIFI_REASON_AUTH_FAIL: return "Authentication failed. Wrong password or unsupported security";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Wrong password (handshake timed out)";
    case WIFI_REASON_AUTH_EXPIRE: return "Authentication expired";
    case WIFI_REASON_AUTH_LEAVE: return "Router disconnected the device";
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_TOOMANY: return "Router refused the device (too many clients?)";
    case WIFI_REASON_ASSOC_FAIL: return "Router refused the connection";
    case WIFI_REASON_BEACON_TIMEOUT: return "Signal lost (router restarted or out of range)";
    case WIFI_REASON_CONNECTION_FAIL: return "Connection failed";
    default: return WiFi.disconnectReasonName((wifi_err_reason_t)r);
  }
}

const char* stateName(StaState s) {
  switch (s) {
    case StaState::NoCredentials: return "not_configured";
    case StaState::Connecting: return "connecting";
    case StaState::Connected: return "connected";
    case StaState::WaitingRetry: return "waiting_retry";
    case StaState::Testing: return "testing";
  }
  return "unknown";
}

const char* requestName(Request r) {
  switch (r) {
    case Request::Accepted: return "ok";
    case Request::Locked: return "locked";
    case Request::Busy: return "busy";
    case Request::Invalid: return "invalid";
  }
  return "error";
}

const char* testStateName(TestState s) {
  switch (s) {
    case TestState::None: return "none";
    case TestState::Pending: return "pending";
    case TestState::Ok: return "ok";
    case TestState::Failed: return "failed";
  }
  return "unknown";
}

}  // namespace wifimgr
