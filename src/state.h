#pragma once
// Shared types between the UI task (core 1) and the network task (core 0).
//
// Rule: core 1 never touches files, NVS, WiFi or BLE. Core 0 does all of that and
// hands finished data over through queues. Small status blocks are copied under gMux.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "agenda.h"
#include "weather.h"

#define FW_VERSION "0.1.0"

// ------------------------------------------------------------------ layout (shortcut pages + timer presets)
#define MAX_PAGES   6
#define MAX_SLOTS   6
#define MAX_PRESETS 6

struct Slot {
  char label[8];    // shown on the round button (up to 6 chars used)
  char hid[64];     // keystrokes sent over Bluetooth (see keycombo.h); empty if unused
  char mac[24];     // id of an action defined in the Mac helper's actions.json; empty if unused
};
struct Page {
  char     name[12];
  uint32_t color;   // 0xRRGGBB
  uint8_t  n;
  Slot     slot[MAX_SLOTS];
};
struct Preset {
  char     label[10];
  uint32_t secs;
};
struct Layout {
  uint8_t pageCount;
  Page    page[MAX_PAGES];
  uint8_t presetCount;
  Preset  preset[MAX_PRESETS];
};

// ------------------------------------------------------------------ core 0 -> core 1
enum UiMsgType : uint8_t { UI_LAYOUT, UI_TOAST, UI_ERROR, UI_NOTE };
struct UiMsg {
  UiMsgType type;
  Layout*   layout;     // UI_LAYOUT: the UI takes ownership and deletes the old one
  char      text[40];   // UI_TOAST / UI_ERROR; UI_NOTE: the title
  char      text2[24];  // UI_NOTE: the app name
};

// ------------------------------------------------------------------ core 1 -> core 0
enum NetCmdType : uint8_t {
  NC_HID,            // a = shortcut string
  NC_ACTION,         // a = Mac helper action id
  NC_NOTIFY,         // a = title, b = body (desktop notification on the Mac)
  NC_SAVE_TIMER,     // n1 = state, n2 = duration s, n3 = remaining s (all 0 = clear)
  NC_SAVE_BRIGHT,    // persist brightness
  NC_CAL_REFRESH,    // fetch the calendar now
  NC_WX_REFRESH      // fetch the weather now
};
struct NetCmd {
  NetCmdType type;
  int32_t    n1, n2, n3;
  char       a[64];
  char       b[48];
};

extern QueueHandle_t qToUi;    // UiMsg
extern QueueHandle_t qToNet;   // NetCmd

// ------------------------------------------------------------------ status blocks
enum CalState : uint8_t { CAL_NOT_SET = 0, CAL_PENDING, CAL_OK, CAL_ERROR };

struct NetStatus {
  char   ip[20];
  char   ssid[34];
  int8_t rssi;
  char   apPin[12];      // random WPA2 password of the setup AP, shown on the device screen
  bool   apMode;         // setup AP currently running
  bool   setupNeeded;    // no WiFi credentials saved yet (first boot)
  bool   connected;
  bool   timeSynced;
  bool   bleOn;          // Bluetooth keyboard enabled in settings (advertising until a Mac connects)
  bool   bleConnected;
  bool   helperSet;      // a Mac helper address is configured
  bool   helperOk;       // last call to the Mac helper worked
  uint8_t calState;      // CalState
  char   calErr[96];     // why the last calendar fetch failed (empty when it worked)
  uint8_t wxState;       // CalState values reused: NOT_SET = weather off
  char   wxErr[80];      // why the last weather fetch failed
  char   wxPlace[40];    // city name for the weather screen (ASCII)
};
struct MacStats {
  float    cpu, ram;     // percent 0..100, pushed by the Mac helper
  uint32_t updatedMs;    // millis() of the last push, 0 = never
};

extern portMUX_TYPE gMux;
extern NetStatus    gNet;
extern MacStats     gMac;
extern Agenda       gAgenda;
extern Weather      gWeather;
