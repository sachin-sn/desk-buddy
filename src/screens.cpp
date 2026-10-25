// LVGL screens: clock, shortcuts, timer, agenda, alert, settings, toast.
// (The buddy itself is drawn by buddy.cpp; scrBuddy is just a black placeholder while it runs.)
#include "ui_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *scrBuddy, *scrClock, *scrShort, *scrTimer, *scrAgenda, *scrAlert, *scrSettings;

// ------------------------------------------------------------------ helpers
static lv_obj_t* newScreen(uint32_t bg = 0x000000) {
  lv_obj_t* s = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_radius(s, 0, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

static lv_obj_t* newLabel(lv_obj_t* parent, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t* newRing(lv_obj_t* parent, int r, int w, uint32_t color, uint32_t track, int range) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, r * 2, r * 2);
  lv_obj_center(a);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, range);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(track), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  return a;
}

// lv_label_set_text always invalidates, so only touch the label when the text changed.
static void setText(lv_obj_t* l, char* cache, size_t n, const char* text) {
  if (strlen(text) < n && strcmp(cache, text) == 0) return;
  strlcpy(cache, text, n);
  lv_label_set_text(l, text);
}

// ------------------------------------------------------------------ clock
static lv_obj_t *arcSec, *lblTime, *lblDate, *lblNext, *lblWx;
static char cTime[8], cDate[20], cNext[40], cWx[40];

static void buildClock() {
  scrClock = newScreen();
  arcSec = newRing(scrClock, 114, 5, 0x00D1FF, 0x14181C, 60);
  lblTime = newLabel(scrClock, &lv_font_montserrat_48, 0xFFFFFF);
  lv_obj_align(lblTime, LV_ALIGN_CENTER, 0, -14);
  lblDate = newLabel(scrClock, &lv_font_montserrat_20, 0x9AA0A6);
  lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, 30);
  lblNext = newLabel(scrClock, &lv_font_montserrat_14, 0x00D1FF);
  lv_label_set_long_mode(lblNext, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblNext, 170);
  lv_obj_align(lblNext, LV_ALIGN_CENTER, 0, 64);
  lblWx = newLabel(scrClock, &lv_font_montserrat_14, 0x9AA0A6);          // weather, above the time
  lv_label_set_long_mode(lblWx, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblWx, 160);
  lv_obj_set_style_text_align(lblWx, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lblWx, LV_ALIGN_CENTER, 0, -66);
}

void clockRefresh(const Snapshot& s, const char* nextLine, uint32_t nextColor) {
  char b[24];
  if (s.timeValid) strftime(b, sizeof b, "%H:%M", &s.t); else snprintf(b, sizeof b, "--:--");
  setText(lblTime, cTime, sizeof cTime, b);
  lv_obj_align(lblTime, LV_ALIGN_CENTER, 0, -14);
  if (s.timeValid) strftime(b, sizeof b, "%a %d %b", &s.t); else snprintf(b, sizeof b, "waiting for time");
  setText(lblDate, cDate, sizeof cDate, b);
  lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, 30);
  int sec = s.timeValid ? s.t.tm_sec : 0;
  if (lv_arc_get_value(arcSec) != sec) lv_arc_set_value(arcSec, sec);
  lv_obj_set_style_text_color(lblNext, lv_color_hex(nextColor), 0);
  setText(lblNext, cNext, sizeof cNext, nextLine);
  lv_obj_align(lblNext, LV_ALIGN_CENTER, 0, 64);

  // "28°C Partly cloudy", in a colour that hints at the weather; empty (hidden) when off or out of date
  char w[40] = "";
  uint32_t wc = 0x9AA0A6;
  if (s.timeValid && weatherFresh(s.wx, s.now)) {
    snprintf(w, sizeof w, "%d" WX_DEG "%c %s", (int)s.wx.temp, s.wx.unit, weatherWords(s.wx.code, s.wx.isDay));
    wc = weatherColor(s.wx.code, s.wx.isDay);
  }
  lv_obj_set_style_text_color(lblWx, lv_color_hex(wc), 0);
  setText(lblWx, cWx, sizeof cWx, w);
  lv_obj_align(lblWx, LV_ALIGN_CENTER, 0, -66);
}

// ------------------------------------------------------------------ shortcuts
static const int RING_R = 70, BTN_D = 62, HIT_R = 34;
static lv_obj_t *sBtn[MAX_SLOTS], *sBtnLbl[MAX_SLOTS], *lblPage, *lblPageNo;
static bool     sShown[MAX_SLOTS];
static uint32_t sColor[MAX_SLOTS];
static int      flashSlot = -1;
static uint32_t flashUntil = 0;

static void slotCenter(int i, int& x, int& y) {
  float a = (-90.f + 60.f * (float)i) * (float)M_PI / 180.f;
  x = 120 + (int)lroundf(cosf(a) * RING_R);
  y = 120 + (int)lroundf(sinf(a) * RING_R);
}

static void styleButton(int i, bool pressed) {
  lv_obj_t* b = sBtn[i];
  lv_color_t c = lv_color_hex(sColor[i]);
  lv_obj_set_style_bg_color(b, c, 0);
  lv_obj_set_style_bg_opa(b, pressed ? LV_OPA_COVER : LV_OPA_20, 0);
  lv_obj_set_style_border_color(b, c, 0);
  lv_obj_set_style_text_color(sBtnLbl[i], pressed ? lv_color_black() : lv_color_white(), 0);
}

static void buildShortcuts() {
  scrShort = newScreen();
  for (int i = 0; i < MAX_SLOTS; i++) {
    int x, y;
    slotCenter(i, x, y);
    lv_obj_t* b = lv_obj_create(scrShort);
    lv_obj_set_size(b, BTN_D, BTN_D);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);      // touches are handled by input.cpp, not LVGL
    lv_obj_align(b, LV_ALIGN_CENTER, x - 120, y - 120);
    lv_obj_t* l = newLabel(b, &lv_font_montserrat_14, 0xFFFFFF);
    lv_obj_center(l);
    sBtn[i] = b; sBtnLbl[i] = l; sColor[i] = 0x00D1FF; sShown[i] = false;
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
  }
  lblPage = newLabel(scrShort, &lv_font_montserrat_20, 0xFFFFFF);
  lv_obj_align(lblPage, LV_ALIGN_CENTER, 0, -8);
  lblPageNo = newLabel(scrShort, &lv_font_montserrat_14, 0x777777);
  lv_obj_align(lblPageNo, LV_ALIGN_CENTER, 0, 16);
}

void shortcutsShow(const Layout* lay, int page) {
  if (!lay || lay->pageCount == 0) {
    for (int i = 0; i < MAX_SLOTS; i++) { sShown[i] = false; lv_obj_add_flag(sBtn[i], LV_OBJ_FLAG_HIDDEN); }
    lv_label_set_text(lblPage, "No shortcuts");
    lv_label_set_text(lblPageNo, "");
    return;
  }
  if (page < 0 || page >= lay->pageCount) page = 0;
  const Page& p = lay->page[page];
  lv_label_set_text(lblPage, p.name);
  lv_obj_set_style_text_color(lblPage, lv_color_hex(p.color), 0);
  lv_obj_align(lblPage, LV_ALIGN_CENTER, 0, -8);
  char b[12];
  snprintf(b, sizeof b, "%d / %d", page + 1, lay->pageCount);
  lv_label_set_text(lblPageNo, b);
  lv_obj_align(lblPageNo, LV_ALIGN_CENTER, 0, 16);
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (i < p.n) {
      sShown[i] = true;
      sColor[i] = p.color;
      lv_label_set_text(sBtnLbl[i], p.slot[i].label);
      lv_obj_center(sBtnLbl[i]);
      styleButton(i, false);
      lv_obj_clear_flag(sBtn[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      sShown[i] = false;
      lv_obj_add_flag(sBtn[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  flashSlot = -1;
}

int shortcutsHit(int x, int y) {
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (!sShown[i]) continue;
    int cx, cy;
    slotCenter(i, cx, cy);
    int dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy <= HIT_R * HIT_R) return i;
  }
  return -1;
}

void shortcutsFlash(int slot, uint32_t color, uint32_t nowMs) {
  if (slot < 0 || slot >= MAX_SLOTS || !sShown[slot]) return;
  if (flashSlot >= 0 && flashSlot != slot) styleButton(flashSlot, false);
  (void)color;
  flashSlot = slot;
  flashUntil = nowMs + 180;
  styleButton(slot, true);
}

void shortcutsTick(uint32_t nowMs) {
  if (flashSlot >= 0 && (int32_t)(nowMs - flashUntil) >= 0) {
    styleButton(flashSlot, false);
    flashSlot = -1;
  }
}

// ------------------------------------------------------------------ timer
static lv_obj_t *arcTimer, *lblTimer, *lblTName, *lblTHint;
static char tTime[12], tName[12], tHint[40];

static void buildTimer() {
  scrTimer = newScreen();
  arcTimer = newRing(scrTimer, 112, 10, 0x00D1FF, 0x14181C, 1000);
  lblTName = newLabel(scrTimer, &lv_font_montserrat_20, 0x9AA0A6);
  lv_obj_align(lblTName, LV_ALIGN_CENTER, 0, -52);
  lblTimer = newLabel(scrTimer, &lv_font_montserrat_48, 0xFFFFFF);
  lv_obj_align(lblTimer, LV_ALIGN_CENTER, 0, -4);
  lblTHint = newLabel(scrTimer, &lv_font_montserrat_14, 0x666B73);
  lv_obj_align(lblTHint, LV_ALIGN_CENTER, 0, 52);
}

void timerRefresh(const Countdown& c, const char* presetLabel, uint64_t nowMs) {
  uint32_t shown = c.state() == Countdown::IDLE ? c.duration() : c.remainingSec(nowMs);
  char b[16];
  Countdown::format(b, sizeof b, shown);
  setText(lblTimer, tTime, sizeof tTime, b);
  lv_obj_align(lblTimer, LV_ALIGN_CENTER, 0, -4);
  setText(lblTName, tName, sizeof tName, presetLabel);
  lv_obj_align(lblTName, LV_ALIGN_CENTER, 0, -52);

  uint32_t color;
  const char* hint;
  switch (c.state()) {
    case Countdown::RUNNING: color = 0x00D1FF; hint = "press: pause"; break;
    case Countdown::PAUSED:  color = 0xFFB020; hint = "press: resume\nhold: reset"; break;
    case Countdown::DONE:    color = 0xFF5A4A; hint = "press to dismiss"; break;
    default:                 color = 0x5C7A99; hint = "turn: set time\npress: start"; break;
  }
  lv_obj_set_style_arc_color(arcTimer, lv_color_hex(color), LV_PART_INDICATOR);
  int v = c.state() == Countdown::IDLE ? 1000 : (int)((1.0f - c.progress(nowMs)) * 1000.0f);
  if (lv_arc_get_value(arcTimer) != v) lv_arc_set_value(arcTimer, v);
  setText(lblTHint, tHint, sizeof tHint, hint);
  lv_obj_align(lblTHint, LV_ALIGN_CENTER, 0, 52);
}

// ------------------------------------------------------------------ agenda
#define AG_ROWS 4
static lv_obj_t *lblAgTitle, *lblAgRow[AG_ROWS], *lblAgFoot;
static char aRow[AG_ROWS][40], aFoot[32];

static void buildAgenda() {
  scrAgenda = newScreen();
  lblAgTitle = newLabel(scrAgenda, &lv_font_montserrat_14, 0x777777);
  lv_label_set_text(lblAgTitle, "NEXT UP");
  lv_obj_align(lblAgTitle, LV_ALIGN_CENTER, 0, -92);
  for (int i = 0; i < AG_ROWS; i++) {
    lblAgRow[i] = newLabel(scrAgenda, &lv_font_montserrat_20, 0xFFFFFF);
    lv_label_set_long_mode(lblAgRow[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(lblAgRow[i], 196);
    lv_obj_align(lblAgRow[i], LV_ALIGN_CENTER, 0, -58 + i * 34);
  }
  lblAgFoot = newLabel(scrAgenda, &lv_font_montserrat_14, 0x666B73);
  lv_obj_align(lblAgFoot, LV_ALIGN_CENTER, 0, 92);
}

void agendaRefresh(const Snapshot& s) {
  const Agenda& a = s.agenda;
  char rows[AG_ROWS][40];
  for (int i = 0; i < AG_ROWS; i++) rows[i][0] = 0;
  int first = a.valid ? agendaNext(a, s.now) : -1;

  if (!a.valid) {
    const char* msg = "No calendar yet";
    if (s.net.calState == CAL_PENDING) msg = s.net.connected ? "Fetching..." : "No WiFi";
    else if (s.net.calState == CAL_ERROR) msg = "Calendar error";
    snprintf(rows[1], sizeof rows[1], "%s", msg);
  } else if (first < 0) {
    snprintf(rows[1], sizeof rows[1], "Nothing scheduled");
  } else {
    tm today = s.t;
    for (int r = 0; r < AG_ROWS && first + r < a.n; r++) {
      const Event& e = a.ev[first + r];
      time_t st = (time_t)e.start;
      tm lt;
      localtime_r(&st, &lt);
      bool sameDay = lt.tm_year == today.tm_year && lt.tm_yday == today.tm_yday;
      char when[12];
      if (sameDay) strftime(when, sizeof when, "%H:%M", &lt);
      else strftime(when, sizeof when, "%a %H:%M", &lt);
      snprintf(rows[r], sizeof rows[r], "%s %s", when, e.title);
    }
  }
  bool nowRunning = first >= 0 && a.ev[first].start <= s.now;
  for (int i = 0; i < AG_ROWS; i++) {
    setText(lblAgRow[i], aRow[i], sizeof aRow[i], rows[i]);
    uint32_t col = 0xC9CDD3;
    if (i == 0 && first >= 0) col = nowRunning ? 0xFF8A65 : 0x00D1FF;
    if (!a.valid || first < 0) col = 0x888E96;
    lv_obj_set_style_text_color(lblAgRow[i], lv_color_hex(col), 0);
  }
  char foot[32];
  if (a.fetched == 0) snprintf(foot, sizeof foot, "%s", s.net.calState == CAL_NOT_SET ? "set up in the web page" : "");
  else {
    int mins = (int)((s.now - a.fetched) / 60);
    if (mins < 1) snprintf(foot, sizeof foot, "synced just now");
    else if (mins < 60) snprintf(foot, sizeof foot, "synced %dm ago", mins);
    else snprintf(foot, sizeof foot, "synced %dh ago", mins / 60);
    if (s.net.calState == CAL_ERROR) snprintf(foot, sizeof foot, "offline - last sync %dm ago", mins);
  }
  setText(lblAgFoot, aFoot, sizeof aFoot, foot);
}

// ------------------------------------------------------------------ alert
static lv_obj_t *lblAl1, *lblAl2;

static void buildAlert() {
  scrAlert = newScreen(0xB3261E);
  lblAl1 = newLabel(scrAlert, &lv_font_montserrat_28, 0xFFFFFF);
  lv_label_set_long_mode(lblAl1, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblAl1, 190);
  lv_obj_align(lblAl1, LV_ALIGN_CENTER, 0, -18);
  lblAl2 = newLabel(scrAlert, &lv_font_montserrat_20, 0xFFFFFF);
  lv_obj_align(lblAl2, LV_ALIGN_CENTER, 0, 62);
}

void alertShow(const char* line1, const char* line2, uint32_t bgColor) {
  lv_obj_set_style_bg_color(scrAlert, lv_color_hex(bgColor), 0);
  lv_label_set_text(lblAl1, line1);
  lv_obj_align(lblAl1, LV_ALIGN_CENTER, 0, -18);
  lv_label_set_text(lblAl2, line2);
  lv_obj_align(lblAl2, LV_ALIGN_CENTER, 0, 62);
}

// ------------------------------------------------------------------ settings
static lv_obj_t* lblSet;
static char sText[160];

static void buildSettings() {
  scrSettings = newScreen();
  lblSet = newLabel(scrSettings, &lv_font_montserrat_20, 0xFFFFFF);
  lv_obj_center(lblSet);
}

void settingsRefresh(const Snapshot& s, uint8_t bright, bool sleeping) {
  char b[160];
  const NetStatus& n = s.net;
  if (n.apMode) {      // setup access point is up: first boot, or the saved WiFi cannot be reached
    snprintf(b, sizeof b, "%s\njoin WiFi\nCrowPanel-Buddy\npass %s\nthen 192.168.4.1", n.setupNeeded ? "SETUP" : "NO WIFI", n.apPin);
  } else {
    char cal[20], mac[20];
    if (s.net.calState == CAL_NOT_SET) snprintf(cal, sizeof cal, "not set");
    else if (s.net.calState == CAL_ERROR) snprintf(cal, sizeof cal, "error");
    else if (!s.agenda.valid) snprintf(cal, sizeof cal, "pending");
    else snprintf(cal, sizeof cal, "%dm ago", (int)((s.now - s.agenda.fetched) / 60));
    snprintf(mac, sizeof mac, "%s", n.helperOk ? "online" : "offline");
    snprintf(b, sizeof b, "%s\nBT %s  Mac %s\nCal %s\nBright %d%s",
             n.connected ? n.ip : "no WiFi",
             !n.bleOn ? "off" : n.bleConnected ? "ok" : "pair", mac, cal, bright, sleeping ? " (night)" : "");
  }
  setText(lblSet, sText, sizeof sText, b);
  lv_obj_center(lblSet);
}

// ------------------------------------------------------------------ toast (top layer, shows over LVGL screens)
static lv_obj_t* lblToast;

static void buildToast() {
  lblToast = newLabel(lv_layer_top(), &lv_font_montserrat_20, 0xFFFFFF);
  lv_obj_set_style_bg_color(lblToast, lv_color_hex(0x202228), 0);
  lv_obj_set_style_bg_opa(lblToast, LV_OPA_90, 0);
  lv_obj_set_style_radius(lblToast, 12, 0);
  lv_obj_set_style_pad_hor(lblToast, 12, 0);
  lv_obj_set_style_pad_ver(lblToast, 6, 0);
  lv_label_set_long_mode(lblToast, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(lblToast, 210, 0);
  lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
}

void toastShow(const char* text) {
  lv_label_set_text(lblToast, text);
  lv_obj_align(lblToast, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_clear_flag(lblToast, LV_OBJ_FLAG_HIDDEN);
}
void toastHide() { lv_obj_add_flag(lblToast, LV_OBJ_FLAG_HIDDEN); }

// ------------------------------------------------------------------ all screens
void screensBuild() {
  scrBuddy = newScreen();
  buildClock();
  buildShortcuts();
  buildTimer();
  buildAgenda();
  buildAlert();
  buildSettings();
  buildToast();
}

lv_obj_t* screenFor(uint8_t mode) {
  switch (mode) {
    case MODE_CLOCK:     return scrClock;
    case MODE_SHORTCUTS: return scrShort;
    case MODE_TIMER:     return scrTimer;
    case MODE_AGENDA:    return scrAgenda;
    case MODE_ALERT:     return scrAlert;
    case MODE_SETTINGS:  return scrSettings;
    default:             return scrBuddy;
  }
}
