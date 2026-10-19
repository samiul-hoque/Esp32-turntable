// secrets_example.h -> copy to secrets.h (gitignored). Optional.
// Same format as the v1 firmware, so an existing secrets.h drops straight in.
// Wi-Fi credentials here only seed the device on first boot; after that they
// live in flash and are changed from the companion app or the setup page.
#pragma once

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Optional overrides (defaults are in config.h):
// #define AP_PASSWORD  "turntable"   // fallback hotspot password (min 8 chars)
// #define OTA_PASSWORD "change-me"   // /update page password (user: admin)
