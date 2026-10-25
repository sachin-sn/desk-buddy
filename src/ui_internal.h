#pragma once
// Shared between ui.cpp (modes, input, timing) and screens.cpp (LVGL objects).
#include <lvgl.h>
#include <time.h>
#include "state.h"
#include "countdown.h"
#include "agenda.h"

enum Mode : uint8_t {
  MODE_BUDDY = 0, MODE_CLOCK, MODE_SHORTCUTS, MODE_TIMER, MODE_AGENDA,   // swipe carousel, in this order
  MODE_CAROUSEL_COUNT,
  MODE_ALERT = 50, MODE_SETTINGS = 51
};

// Everything a screen needs for one refresh, copied once per tick.
struct Snapshot {
  tm        t;
  bool      timeValid;
  uint32_t  now;        // unix seconds
  NetStatus net;
  MacStats  mac;
  Agenda    agenda;
  Weather   wx;
};

// ---- screens.cpp ----
void      screensBuild();
lv_obj_t* screenFor(uint8_t mode);

void clockRefresh(const Snapshot& s, const char* nextLine, uint32_t nextColor);

void shortcutsShow(const Layout* lay, int page);        // (re)fills the six buttons for a page
int  shortcutsHit(int x, int y);                        // slot under a touch point, or -1
void shortcutsFlash(int slot, uint32_t color, uint32_t nowMs);
void shortcutsTick(uint32_t nowMs);

void timerRefresh(const Countdown& c, const char* presetLabel, uint64_t nowMs);

void agendaRefresh(const Snapshot& s);

void alertShow(const char* line1, const char* line2, uint32_t bgColor);

void settingsRefresh(const Snapshot& s, uint8_t bright, bool sleeping);

void toastShow(const char* text);
void toastHide();
