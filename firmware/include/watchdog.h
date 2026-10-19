// Software watchdog for the network task (web server, Wi-Fi manager, BLE pump).
// If that task stops making progress, the device would look dead on Wi-Fi and the
// app; motion and BLE Stop keep working, but nothing else does. After a long stall
// (and only while the turntable is idle) the device restarts and records why.
#pragma once
#include <stdint.h>

namespace watchdog {
void begin();
void beat(const char* stage);  // network task: call at least every few seconds
const char* lastFault();       // fault recorded before the last restart, or ""
}
