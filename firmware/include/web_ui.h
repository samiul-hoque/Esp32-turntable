// HTTP server: control page, legacy /rotate, JSON API, captive portal, OTA.
// Network task only.
#pragma once
#include <WebServer.h>

namespace web {
void begin();
void tick();
WebServer& server();
}
