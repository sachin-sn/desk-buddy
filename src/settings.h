#pragma once
#include <Arduino.h>

// Persistent settings (NVS). Strings are only touched from core 0; the single-byte
// fields are read from core 1 as well (single-byte reads are atomic).
struct Settings {
  String ssid, pass, adminPass, host, tz;
  String calUrl;                 // Google Apps Script web-app URL including ?t=TOKEN
  String helperHost;             // Mac helper: hostname.local or IPv4
  uint16_t helperPort = 8765;
  String apiToken;               // shared secret between this device and the Mac helper

  uint8_t bright      = 200;     // 10..255
  uint8_t nightBright = 12;      // used while the buddy sleeps
  uint8_t sleepFrom   = 23;      // hour the buddy goes to sleep
  uint8_t sleepTo     = 7;       // hour it wakes up
  uint8_t leadMin     = 10;      // meeting warning, minutes before the start (0 = off)
  uint8_t clockSecs   = 15;      // clock screen returns to the buddy after this many seconds
  bool    ledOn       = true;    // WS2812 ring
  bool    bleHid      = true;
};

extern Settings gCfg;

void settingsLoad();
void settingsSave();                       // core 0 only
String settingsLoadLayoutJson();           // "" if none saved
void settingsSaveLayoutJson(const String& json);

// Timer persistence (core 0 only). state: 0 idle, 1 running, 2 paused, 3 done.
struct SavedTimer { uint8_t state; uint32_t dur; uint32_t rem; uint32_t savedAt; };
SavedTimer settingsLoadTimer();
void settingsSaveTimer(uint8_t state, uint32_t dur, uint32_t rem, uint32_t nowEpoch);
