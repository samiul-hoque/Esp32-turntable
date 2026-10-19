// Password-protected firmware upload at /update.
#pragma once
#include <WebServer.h>

namespace ota {
void begin(WebServer& server);
}
