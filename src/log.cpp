#include "log.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdarg.h>
#include <time.h>

static LogRing     ring;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void logEvent(const char* fmt, ...) {
  char buf[LOG_WIDTH];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);

  Serial.print("[log] ");
  Serial.println(buf);

  time_t t = time(nullptr);
  uint32_t epoch = t > 1735689600 ? (uint32_t)t : 0;      // 1 Jan 2025: anything earlier means the clock is not set
  portENTER_CRITICAL(&logMux);
  ring.push(millis() / 1000, epoch, buf);
  portEXIT_CRITICAL(&logMux);
}

int logRead(LogEntry* out, int max) {
  portENTER_CRITICAL(&logMux);
  int n = ring.size();
  if (n > max) n = max;
  int skip = ring.size() - n;                               // if the caller's buffer is small, keep the newest lines
  for (int i = 0; i < n; i++) out[i] = ring.at(skip + i);
  portEXIT_CRITICAL(&logMux);
  return n;
}

const char* logResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "CRASH (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:                return "other";
  }
}
