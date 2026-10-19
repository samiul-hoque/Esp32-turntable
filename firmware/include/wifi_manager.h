// Wi-Fi connection manager. All functions except begin() must be called from the
// network task only (it owns the Wi-Fi driver, NVS writes and the hotspot).
#pragma once
#include <stdint.h>

namespace wifimgr {

enum class StaState : uint8_t {
  NoCredentials,  // nothing stored: hotspot only
  Connecting,     // background (re)connect attempt
  Connected,
  WaitingRetry,   // between attempts (backoff)
  Testing,        // trying newly provisioned credentials
};

struct Info {
  StaState state;
  bool hasCredentials;
  char ssid[33];       // stored network
  char ip[16];         // station IP when connected
  int8_t rssi;
  uint8_t channel;
  uint8_t lastReason;  // last disconnect reason code, 0 = none/timeout
  uint32_t nextRetrySec;
  bool apActive;
  char apSsid[33];
  char apIp[16];
  uint8_t apClients;
  bool setupAllowed;
};

enum class TestState : uint8_t { None, Pending, Ok, Failed };

struct TestResult {
  uint32_t id;
  TestState state;
  char ssid[33];
  uint8_t reason;      // when Failed
  char ip[16];         // when Ok
};

struct Network {
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  bool secure;
};

enum class Request : uint8_t { Accepted, Locked, Busy, Invalid };

void begin();
void tick();

Info info();
TestResult lastTest();
uint32_t changeCounter();   // increments whenever info() or lastTest() changes

// Provisioning (subject to the setup window, see config.h).
Request setCredentials(const char* ssid, const char* pass, const char** error);
Request forget();
bool setupAllowed();

void requestScan();
bool scanInProgress();
uint32_t scanId();          // increments when a scan finishes
int scanResults(Network* out, int max);

const char* reasonText(uint8_t reason);
const char* stateName(StaState s);
const char* requestName(Request r);
const char* testStateName(TestState s);

}  // namespace wifimgr
