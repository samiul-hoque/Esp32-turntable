// Central configuration. Pure constants only: also compiled by the native unit tests.
#pragma once
#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Bump when the BLE/HTTP JSON protocol changes incompatibly; the app checks it.
#define PROTO_VERSION 1

// === Stepper Motor Configuration ===
#define STEP_PIN 18
#define DIR_PIN 19
#define ENABLE_PIN 21

const int microsteps = 16;
const int stepsPerRev = 200 * microsteps;
const float gearRatio = 4.0; // Turntable:Motor ratio

// === Safety Limits (modify these values in firmware) ===
const float MAX_SPEED_RPM = 60.0;               // Maximum allowed speed in RPM
const float MAX_ACCELERATION_RPM_S = 30.0;      // Maximum acceleration in RPM/s
const float DEFAULT_ACCELERATION_RPM_S = 10.0;  // Default acceleration if not specified
const float DEFAULT_DECELERATION_RPM_S = 10.0;  // Default deceleration if not specified

// === Identity ===
#define HOSTNAME "turntable"                    // http://turntable.local
#define DEVICE_NAME_PREFIX "Auleek-Turntable-"  // + 4 hex digits of the MAC (BLE name and hotspot SSID)

// === Wi-Fi ===
// Regulatory domain: "01" = world-safe. Channels 1-13 with MANUAL policy so a
// neighbouring AP advertising a different country can't shrink the channel list.
#define WIFI_COUNTRY "01"
#define WIFI_CHANNEL_FIRST 1
#define WIFI_CHANNEL_COUNT 13

const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;    // one connection attempt
const uint32_t WIFI_ATTEMPT_SETTLE_MS = 4000;      // attempt is over this long after a disconnect with no reconnect
const uint32_t WIFI_TEST_TIMEOUT_MS = 25000;       // testing newly provisioned credentials
const uint32_t WIFI_AP_FALLBACK_MS = 30000;        // offline this long -> start the setup hotspot
const uint32_t WIFI_AP_LINGER_MS = 60000;          // keep the hotspot up after STA connects (so the setup page can show the result)
const uint32_t WIFI_RETRY_WITH_AP_CLIENT_MS = 300000; // retry STA at most this often while a phone is on the hotspot
const uint32_t WIFI_RETRY_BACKOFF_MS[] = {5000, 10000, 30000, 60000};
const uint32_t WIFI_AUTH_FAIL_BACKOFF_MS = 300000; // after repeated wrong-password failures
const uint8_t  WIFI_AUTH_FAIL_LIMIT = 3;
const int      WIFI_MAX_SCAN_RESULTS = 20;

// Wi-Fi credentials may only be changed within this window after power-on,
// or at any time while the device is not connected to Wi-Fi.
const uint32_t SETUP_WINDOW_MS = 10UL * 60UL * 1000UL;

// Defaults; override in secrets.h with #define AP_PASSWORD / OTA_PASSWORD.
#define DEFAULT_AP_PASSWORD "turntable"        // WPA2, min 8 characters
#define DEFAULT_OTA_PASSWORD "auleek-update"   // /update page, user "admin"
#define OTA_USER "admin"

// === BLE ===
#define BLE_SERVICE_UUID "6e7a0001-9b3c-4f1e-8a2d-5c1f0e7b2a10"
#define BLE_COMMAND_UUID "6e7a0002-9b3c-4f1e-8a2d-5c1f0e7b2a10"  // write: one JSON command per write
#define BLE_STATE_UUID   "6e7a0003-9b3c-4f1e-8a2d-5c1f0e7b2a10"  // read: current state JSON
#define BLE_EVENTS_UUID  "6e7a0004-9b3c-4f1e-8a2d-5c1f0e7b2a10"  // notify: newline-delimited JSON events
const uint32_t BLE_NOTIFY_SPACING_MS = 15;
const uint32_t BLE_PROGRESS_INTERVAL_MS = 250;
