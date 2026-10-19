// Device identity, build-time secrets and diagnostics.
#pragma once
#include <stdint.h>

namespace device {
void begin();
const char* name();          // "Auleek-Turntable-1A2B" (BLE name and hotspot SSID)
const char* resetReason();   // why the chip last restarted, e.g. "power_on", "brownout", "panic"
uint32_t uptimeSec();

// From secrets.h if present, otherwise defaults / empty.
const char* seedSsid();
const char* seedPassword();
const char* apPassword();
const char* otaPassword();
}
