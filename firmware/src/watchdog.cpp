#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include "motion.h"
#include "watchdog.h"

static const uint32_t REPORT_AFTER_MS = 5000;   // log a stall
static const uint32_t RESTART_AFTER_MS = 60000; // restart (if idle)
static const uint32_t FAULT_MAGIC = 0x7AB1E5ED;

// Survives a software restart (not a power cycle), so the next boot can report it.
RTC_NOINIT_ATTR static uint32_t s_faultMagic;
RTC_NOINIT_ATTR static char s_faultText[64];

static char s_lastFault[64] = "";
static volatile uint32_t s_beat = 0;
static volatile const char* s_stage = "start";

static void monitorTask(void*) {
  uint32_t lastReport = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t now = millis();
    uint32_t stalled = now - s_beat;
    if (stalled < REPORT_AFTER_MS) continue;

    const char* stage = (const char*)s_stage;
    if (now - lastReport >= 5000) {
      lastReport = now;
      Serial.printf("[watchdog] network task stalled %lu s in '%s' (heap %u)\n",
                    (unsigned long)(stalled / 1000), stage, ESP.getFreeHeap());
    }
    if (stalled >= RESTART_AFTER_MS && !motion::isBusy()) {
      snprintf(s_faultText, sizeof(s_faultText), "network task stalled in '%s'", stage);
      s_faultMagic = FAULT_MAGIC;
      Serial.printf("[watchdog] restarting: %s\n", s_faultText);
      Serial.flush();
      esp_restart();
    }
  }
}

namespace watchdog {

void begin() {
  if (s_faultMagic == FAULT_MAGIC && esp_reset_reason() == ESP_RST_SW) {
    s_faultText[sizeof(s_faultText) - 1] = 0;
    strlcpy(s_lastFault, s_faultText, sizeof(s_lastFault));
    Serial.printf("[watchdog] previous restart: %s\n", s_lastFault);
  }
  s_faultMagic = 0;
  s_beat = millis();
  xTaskCreatePinnedToCore(monitorTask, "watchdog", 2560, nullptr, 2, nullptr, 0);
}

void beat(const char* stage) {
  s_stage = stage;
  s_beat = millis();
}

const char* lastFault() { return s_lastFault; }

}  // namespace watchdog
