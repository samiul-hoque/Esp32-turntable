#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "device.h"

// secrets.h defines globals, so it is included in this one translation unit only.
#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_SECRETS 1
#else
#define HAVE_SECRETS 0
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD DEFAULT_AP_PASSWORD
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD DEFAULT_OTA_PASSWORD
#endif

static char s_name[32];

namespace device {

void begin() {
  uint64_t mac = ESP.getEfuseMac();  // bytes in little-endian order: byte 5 is the last MAC byte
  uint8_t b4 = (mac >> 32) & 0xFF;
  uint8_t b5 = (mac >> 40) & 0xFF;
  snprintf(s_name, sizeof(s_name), "%s%02X%02X", DEVICE_NAME_PREFIX, b4, b5);
}

const char* name() { return s_name; }

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

uint32_t uptimeSec() { return millis() / 1000; }

const char* seedSsid() {
#if HAVE_SECRETS
  if (strcmp(ssid, "YOUR_WIFI_SSID") != 0) return ssid;
#endif
  return "";
}

const char* seedPassword() {
#if HAVE_SECRETS
  return password;
#else
  return "";
#endif
}

const char* apPassword() { return AP_PASSWORD; }
const char* otaPassword() { return OTA_PASSWORD; }

}  // namespace device
