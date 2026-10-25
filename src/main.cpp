// CrowPanel 1.28" Desk Buddy
//
//   core 1: ui.cpp   buddy eyes, LVGL screens, touch, knob, LED ring   (never blocks)
//   core 0: net.cpp  WiFi, web server, NTP, calendar, Mac helper, BLE keyboard, NVS writes
//
// The two cores only talk through the two queues and the small status blocks below.
#include <Arduino.h>
#include <LittleFS.h>
#include "log.h"
#include "net.h"
#include "pins.h"
#include "settings.h"
#include "state.h"
#include "ui.h"

QueueHandle_t qToUi  = nullptr;
QueueHandle_t qToNet = nullptr;
portMUX_TYPE  gMux = portMUX_INITIALIZER_UNLOCKED;
NetStatus     gNet{};
MacStats      gMac{};
Agenda        gAgenda{};

void setup() {
  Serial.begin(115200);

  // Switch the LCD/touch 3.3V rail (and the external 5V pins) on before anything talks to
  // the display. Same order as Elecrow's example. The power LED is driven LOW there too.
  pinMode(PIN_PWR_LED, OUTPUT);   digitalWrite(PIN_PWR_LED, LOW);
  pinMode(PIN_LCD_PWR, OUTPUT);   digitalWrite(PIN_LCD_PWR, HIGH);
  pinMode(PIN_EXT5V_PWR, OUTPUT); digitalWrite(PIN_EXT5V_PWR, HIGH);
  delay(200);   // also lets the rail settle

  logf("boot: last restart was %s; free memory %u KB, PSRAM %u KB", logResetReason(),
       (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  if (!LittleFS.begin(true)) logf("storage: the web-editor files could not be mounted (run: pio run -t uploadfs)");   // true = format on first use
  settingsLoad();

  qToUi  = xQueueCreate(6, sizeof(UiMsg));
  qToNet = xQueueCreate(16, sizeof(NetCmd));

  uiInit();

  // UI on core 1 (Arduino's loop() also lives there but is unused), network on core 0.
  xTaskCreatePinnedToCore(uiTask,  "ui",  16384, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 20480, nullptr, 2, nullptr, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);   // nothing to do here; everything runs in the two tasks
}
