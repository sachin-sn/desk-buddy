#pragma once
// Shared between ui.cpp (modes, input, timing) and screens.cpp (LVGL objects).
#include <lvgl.h>
#include <time.h>
#include "state.h"
#include "countdown.h"
#include "agenda.h"

enum Mode : uint8_t {
  MODE_BUDDY = 0, MODE_CLOCK, MODE_SHORTCUTS, MODE_TIMER, MODE_AGENDA, MODE_WEATHER,   // swipe carousel, in this order
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

lv_obj_t* newLabel(lv_obj_t* parent, const lv_font_t* f, uint32_t color);
lv_obj_t* newRing(lv_obj_t* parent, int r, int w, uint32_t color, uint32_t track, int range);

// ---- faces.cpp: the clock screen's faces (see faceids.h)
struct ClockView {
  const Snapshot* s;
  const char*     nextLine;     // "Standup  in 12m" or empty
  uint32_t        nextColor;
  uint32_t        nowMs;        // millis(): drives the animations
  uint8_t         face;         // FaceId
  uint32_t        accent;       // 0xRRGGBB
  uint8_t         photoDim;     // percent
};
void facesInit(lv_obj_t* clockScreen);     // once, from screens.cpp
void clockRefresh(const ClockView& v);     // ~30 times a second while the clock is showing
void clockEnter(uint32_t nowMs);           // the clock screen is about to be shown (restarts entry animations)
void clockPhotoSync();                     // while another screen shows: tell the photo store the old buffer is free

void shortcutsShow(const Layout* lay, int page);        // (re)fills the six buttons for a page
int  shortcutsHit(int x, int y);                        // slot under a touch point, or -1
void shortcutsFlash(int slot, uint32_t color, uint32_t nowMs);
void shortcutsTick(uint32_t nowMs);

void timerRefresh(const Countdown& c, const char* presetLabel, uint64_t nowMs);

void agendaRefresh(const Snapshot& s);

void weatherRefresh(const Snapshot& s);

void alertShow(const char* line1, const char* line2, uint32_t bgColor);

void settingsRefresh(const Snapshot& s, uint8_t bright, bool sleeping);

void toastShow(const char* text);
void toastHide();
