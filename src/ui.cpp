// UI controller (core 1): modes, navigation, input, timer, meeting alerts, LED ring.
#include "ui.h"
#include <lvgl.h>
#include <Adafruit_NeoPixel.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "agenda.h"
#include "buddy.h"
#include "countdown.h"
#include "eyes.h"
#include "input.h"
#include "layout.h"
#include "pins.h"
#include "settings.h"
#include "state.h"
#include "ui_internal.h"

LGFX tft;

static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t      dispDrv;
static lv_color_t*        lvBuf = nullptr;
static Adafruit_NeoPixel  strip(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

static const uint32_t PAGE_IDLE_MS = 45000;      // shortcuts / timer / agenda go back to the buddy after this
static const uint32_t SETTINGS_IDLE_MS = 60000;
static const uint32_t TIMER_ALERT_MS = 120000;

enum AlertSeverity : uint8_t { ALERT_NONE = 0, ALERT_SOON = 1, ALERT_START = 2, ALERT_TIMER = 3 };

static uint8_t   mode = MODE_BUDDY, returnMode = MODE_BUDDY;
static Layout*   layout = nullptr;
static int       page = 0, presetIdx = 0;
static bool      customTimer = false;            // duration was changed with the knob, so the preset name no longer applies
static Countdown cd;
static AlertTracker tracker;
static Snapshot  snap;
static uint32_t  lastActivity = 0, toastUntil = 0, brightSaveAt = 0, alertUntil = 0;
static uint8_t   alertKind = ALERT_NONE;
static char      toastText[48];
static uint8_t   appliedBright = 0;
static SavedTimer pendingTimer;
static bool      hasPending = false;

// ------------------------------------------------------------------ display glue
static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&c->full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

// ------------------------------------------------------------------ messages to core 0
static void netCmd(NetCmdType t, const char* a = "", const char* b = "", int32_t n1 = 0, int32_t n2 = 0, int32_t n3 = 0) {
  NetCmd c;
  memset(&c, 0, sizeof c);
  c.type = t; c.n1 = n1; c.n2 = n2; c.n3 = n3;
  strlcpy(c.a, a, sizeof c.a);
  strlcpy(c.b, b, sizeof c.b);
  xQueueSend(qToNet, &c, 0);                 // never block the UI; drop if the queue is full
}

static void saveTimer(uint64_t nowMs) {
  netCmd(NC_SAVE_TIMER, "", "", (int32_t)cd.state(), (int32_t)cd.duration(), (int32_t)cd.remainingSec(nowMs));
}

static void toast(const char* s, uint32_t ms = 1500) {
  strlcpy(toastText, s, sizeof toastText);
  toastUntil = millis() + ms;
  toastShow(toastText);
}

// ------------------------------------------------------------------ helpers
static const char* presetLabel() {
  if (customTimer || !layout || presetIdx >= layout->presetCount) return "Timer";
  return layout->preset[presetIdx].label;
}

static void selectPreset(int idx) {
  if (!layout || layout->presetCount == 0) return;
  presetIdx = (idx + layout->presetCount) % layout->presetCount;
  customTimer = false;
  cd.setDuration(layout->preset[presetIdx].secs);
}

static bool sleeping() {
  return snap.timeValid && eyes::inSleepWindow(snap.t.tm_hour, gCfg.sleepFrom, gCfg.sleepTo);
}

static void takeSnapshot() {
  time_t t = time(nullptr);
  snap.now = (uint32_t)t;
  snap.timeValid = t > 1735689600;           // 2025-01-01: earlier means NTP has not run yet
  localtime_r(&t, &snap.t);
  portENTER_CRITICAL(&gMux);
  snap.net = gNet;
  snap.mac = gMac;
  snap.agenda = gAgenda;
  snap.wx = gWeather;
  portEXIT_CRITICAL(&gMux);
}

// "Standup  in 12m" for the next meeting within `horizonSec`; empty string if there is none.
static void nextMeetingText(char* out, size_t n, uint32_t& color, int titleChars, const char* sep) {
  out[0] = 0;
  color = 0x00D1FF;
  if (!snap.timeValid || !snap.agenda.valid) return;
  int i = agendaNext(snap.agenda, snap.now);
  if (i < 0) return;
  const Event& e = snap.agenda.ev[i];
  char rel[20];
  if (e.start <= snap.now) {
    fmtLeft(rel, sizeof rel, (int32_t)(e.end - snap.now));
    color = 0xFF8A65;
  } else {
    int32_t until = (int32_t)(e.start - snap.now);
    if (until > 12 * 3600) return;
    fmtUntil(rel, sizeof rel, until);
    if (until <= 600) color = 0xFFB020;
  }
  snprintf(out, n, "%.*s%s%s", titleChars, e.title, sep, rel);
}

// ------------------------------------------------------------------ modes
static void gotoMode(uint8_t m) {
  if (m == MODE_BUDDY && !buddyAvailable()) m = MODE_CLOCK;
  mode = m;
  if (m == MODE_BUDDY) buddyEnter(millis());
  if (m == MODE_SHORTCUTS) shortcutsShow(layout, page);
  lv_scr_load(screenFor(m));
  lv_obj_invalidate(lv_scr_act());
}

// The weather screen only joins the carousel once weather is switched on in the web editor.
static bool modeAvailable(uint8_t m) { return m != MODE_WEATHER || gCfg.weatherOn; }

static void navigate(int dir) {
  uint8_t m = mode;
  for (int i = 0; i < MODE_CAROUSEL_COUNT; i++) {
    m = (uint8_t)((m + MODE_CAROUSEL_COUNT + dir) % MODE_CAROUSEL_COUNT);
    if (modeAvailable(m)) break;
  }
  gotoMode(m);
}

static void openSettings() {
  returnMode = mode;
  gotoMode(MODE_SETTINGS);
}

static void closeSettings() {
  gotoMode(returnMode == MODE_SETTINGS || returnMode == MODE_ALERT ? (uint8_t)MODE_BUDDY : returnMode);
}

// ------------------------------------------------------------------ alerts
static void raiseAlert(uint8_t severity, const char* l1, const char* l2, uint32_t bg, uint32_t durMs) {
  if (alertKind != ALERT_NONE && severity <= alertKind) return;     // keep the more important one
  if (mode != MODE_ALERT) returnMode = mode;
  alertKind = severity;
  alertUntil = durMs ? millis() + durMs : 0;
  alertShow(l1, l2, bg);
  mode = MODE_ALERT;
  lv_scr_load(screenFor(MODE_ALERT));
  lv_obj_invalidate(lv_scr_act());
}

static void dismissAlert() {
  if (alertKind == ALERT_TIMER) { cd.reset(); saveTimer(millis()); }
  alertKind = ALERT_NONE;
  alertUntil = 0;
  gotoMode(returnMode == MODE_ALERT || returnMode == MODE_SETTINGS ? (uint8_t)MODE_BUDDY : returnMode);
}

static void timerFinished(bool whileAway) {
  char l2[32], t[12];
  Countdown::format(t, sizeof t, cd.duration());
  snprintf(l2, sizeof l2, "%s  %s", presetLabel(), t);
  raiseAlert(ALERT_TIMER, whileAway ? "FINISHED while away" : "TIME'S UP", l2, 0xB3261E, TIMER_ALERT_MS);
  netCmd(NC_NOTIFY, "Timer finished", l2);
}

static void checkAlerts(uint64_t nowMs) {
  if (cd.tick(nowMs)) { saveTimer(nowMs); timerFinished(false); }

  if (snap.timeValid && snap.agenda.valid) {
    int idx = -1;
    AlertKind ak = agendaAlert(snap.agenda, snap.now, (uint32_t)gCfg.leadMin * 60, tracker, &idx);
    if (ak != AL_NONE && idx >= 0) {
      const Event& e = snap.agenda.ev[idx];
      char l2[24];
      if (ak == AL_SOON) {
        fmtUntil(l2, sizeof l2, (int32_t)(e.start - snap.now));
        raiseAlert(ALERT_SOON, e.title, l2, 0xA66300, 8000);
      } else {
        raiseAlert(ALERT_START, e.title, "starting now", 0x1B7F4B, 10000);
      }
    }
  }
  if (alertKind != ALERT_NONE && alertUntil && (int32_t)(millis() - alertUntil) >= 0) dismissAlert();
}

static void restorePendingTimer(uint64_t nowMs) {
  if (!hasPending) return;
  if (pendingTimer.state == Countdown::PAUSED) {
    cd.restore(Countdown::PAUSED, pendingTimer.dur, pendingTimer.rem, nowMs);
    customTimer = true; hasPending = false;
  } else if (pendingTimer.state == Countdown::RUNNING) {
    if (snap.timeValid && pendingTimer.savedAt > 0) {
      uint32_t away = snap.now > pendingTimer.savedAt ? snap.now - pendingTimer.savedAt : 0;
      uint32_t rem = pendingTimer.rem > away ? pendingTimer.rem - away : 0;
      cd.restore(Countdown::RUNNING, pendingTimer.dur, rem, nowMs);
      customTimer = true; hasPending = false;
      if (cd.state() == Countdown::DONE) { saveTimer(nowMs); timerFinished(true); }
    } else if (!snap.timeValid && nowMs > 90000) {          // no clock after 90 s: keep it, but paused
      cd.restore(Countdown::PAUSED, pendingTimer.dur, pendingTimer.rem, nowMs);
      customTimer = true; hasPending = false;
    }
  } else {
    hasPending = false;
  }
}

// ------------------------------------------------------------------ LED ring
static void updateLeds(uint32_t now, uint64_t nowMs) {
  static uint32_t lastShow = 0;
  static uint32_t prev[LED_COUNT];
  static bool first = true;
  if (now - lastShow < 60) return;
  lastShow = now;

  uint32_t px[LED_COUNT] = {0};
  if (gCfg.ledOn) {
    if (mode == MODE_ALERT) {
      bool on = (now / 250) & 1;
      uint32_t c = alertKind == ALERT_TIMER ? strip.Color(255, 30, 10) : alertKind == ALERT_START ? strip.Color(0, 200, 90) : strip.Color(255, 140, 0);
      for (int i = 0; i < LED_COUNT; i++) px[i] = on ? c : 0;
    } else if (cd.state() == Countdown::RUNNING) {
      float remaining = 1.0f - cd.progress(nowMs);                      // LEDs go out one by one
      for (int i = 0; i < LED_COUNT; i++) {
        float v = remaining * LED_COUNT - i;
        if (v >= 1.f) px[i] = strip.Color(0, 36, 54);
        else if (v > 0.f) px[i] = strip.Color(0, (uint8_t)(36 * v), (uint8_t)(54 * v));
      }
    } else if (!sleeping()) {
      for (int i = 0; i < LED_COUNT; i++) px[i] = strip.Color(0, 5, 9);   // faint ambient glow
    }
  }
  bool changed = first;
  for (int i = 0; i < LED_COUNT; i++) if (px[i] != prev[i]) changed = true;
  if (!changed) return;
  first = false;
  for (int i = 0; i < LED_COUNT; i++) { prev[i] = px[i]; strip.setPixelColor(i, px[i]); }
  strip.show();
}

// ------------------------------------------------------------------ periodic work (every 100 ms)
static void applyBrightness() {
  uint8_t b = gCfg.bright;
  if (sleeping()) b = (mode == MODE_BUDDY) ? gCfg.nightBright : (uint8_t)max((int)gCfg.nightBright, (int)gCfg.bright / 2);
  if (b != appliedBright) { appliedBright = b; tft.setBrightness(b); }
}

static void refresh(uint32_t now) {
  uint64_t nowMs = now;
  takeSnapshot();
  restorePendingTimer(nowMs);
  checkAlerts(nowMs);
  applyBrightness();
  updateLeds(now, nowMs);

  // first boot: nothing else is useful until WiFi is set up, so keep the setup instructions on screen
  if (snap.net.apMode && snap.net.setupNeeded && mode != MODE_SETTINGS && mode != MODE_ALERT) {
    returnMode = MODE_BUDDY;
    gotoMode(MODE_SETTINGS);
  }

  // go back to the buddy when nobody is touching it
  uint32_t idle = now - lastActivity;
  if (mode == MODE_CLOCK && gCfg.clockSecs > 0 && idle > (uint32_t)gCfg.clockSecs * 1000) gotoMode(MODE_BUDDY);
  else if ((mode == MODE_SHORTCUTS || mode == MODE_TIMER || mode == MODE_AGENDA || mode == MODE_WEATHER) && idle > PAGE_IDLE_MS) gotoMode(MODE_BUDDY);
  else if (mode == MODE_SETTINGS && idle > SETTINGS_IDLE_MS) closeSettings();

  switch (mode) {
    case MODE_CLOCK: {
      char next[48];
      uint32_t col;
      nextMeetingText(next, sizeof next, col, 18, "  ");
      clockRefresh(snap, next, col);
      break;
    }
    case MODE_TIMER:    timerRefresh(cd, presetLabel(), nowMs); break;
    case MODE_AGENDA:   agendaRefresh(snap); break;
    case MODE_WEATHER:  weatherRefresh(snap); break;
    case MODE_SETTINGS: settingsRefresh(snap, gCfg.bright, sleeping()); break;
    default: break;
  }

  if (toastUntil && (int32_t)(now - toastUntil) >= 0) { toastUntil = 0; toastText[0] = 0; toastHide(); }
  if (brightSaveAt && (int32_t)(now - brightSaveAt) >= 0) { brightSaveAt = 0; netCmd(NC_SAVE_BRIGHT); }
}

// ------------------------------------------------------------------ buddy frame
static void buddyTick(uint32_t now) {
  static eyes::Mood mood = eyes::NORMAL;
  static uint32_t lastMood = 0;
  static char banner[40];
  static uint16_t bannerColor = 0;

  if (now - lastMood >= 200) {                      // mood and banner change slowly: no need to rebuild every frame
    lastMood = now;
    eyes::MoodInput in;
    in.hour = snap.timeValid ? snap.t.tm_hour : -1;
    in.sleepFrom = gCfg.sleepFrom;
    in.sleepTo = gCfg.sleepTo;
    if (snap.timeValid && snap.agenda.valid) {
      int i = agendaNext(snap.agenda, snap.now);
      if (i >= 0 && snap.agenda.ev[i].start > snap.now) in.minsToMeeting = (int)((snap.agenda.ev[i].start - snap.now) / 60);
    }
    bool macFresh = snap.mac.updatedMs && (now - snap.mac.updatedMs) < 15000;
    in.cpu = macFresh ? (int)snap.mac.cpu : -1;
    in.justTouched = (now - lastActivity) < 15000;
    in.idleMinutes = (now - lastActivity) / 60000;
    mood = eyes::chooseMood(in);

    uint32_t col = 0x8C94A0;
    banner[0] = 0;
    if (cd.state() == Countdown::RUNNING || cd.state() == Countdown::PAUSED) {
      char t[12];
      Countdown::format(t, sizeof t, cd.remainingSec(now));
      snprintf(banner, sizeof banner, "%s %s%s", presetLabel(), t, cd.state() == Countdown::PAUSED ? " (paused)" : "");
      col = 0x00D1FF;
    } else {
      nextMeetingText(banner, sizeof banner, col, 12, " - ");
    }
    bannerColor = eyes::rgb565((col >> 16) & 255, (col >> 8) & 255, col & 255);
  }

  BuddyView v;
  v.mood = mood;
  v.banner = banner;
  v.bannerColor = bannerColor;
  v.toast = toastUntil ? toastText : "";
  buddyFrame(now, v);
}

// ------------------------------------------------------------------ messages from core 0
static void handleMsg(const UiMsg& m) {
  switch (m.type) {
    case UI_LAYOUT: {
      Layout* old = layout;
      layout = m.layout;
      delete old;
      if (page >= layout->pageCount) page = 0;
      if (presetIdx >= layout->presetCount) presetIdx = 0;
      if (cd.state() == Countdown::IDLE && !customTimer) cd.setDuration(layout->preset[presetIdx].secs);
      if (mode == MODE_SHORTCUTS) shortcutsShow(layout, page);
      break;
    }
    case UI_TOAST: toast(m.text, 1500); break;
    case UI_ERROR: toast(m.text, 3500); break;
  }
}

// ------------------------------------------------------------------ input
static void adjustBrightness(int dir) {
  int v = constrain((int)gCfg.bright + dir * 15, 10, 255);
  gCfg.bright = (uint8_t)v;
  brightSaveAt = millis() + 1500;               // persist once the knob stops moving
}

static void fireSlot(int i) {
  if (!layout || page >= layout->pageCount) return;
  const Page& p = layout->page[page];
  if (i < 0 || i >= p.n) return;
  const Slot& s = p.slot[i];
  if (s.hid[0]) {
    if (!snap.net.bleConnected) { toast("Pair Bluetooth first", 2500); return; }
    netCmd(NC_HID, s.hid);
  } else if (s.mac[0]) {
    if (!snap.net.helperSet) { toast("Set up the Mac helper", 2500); return; }
    netCmd(NC_ACTION, s.mac);
  } else {
    return;
  }
  shortcutsFlash(i, p.color, millis());
}

static void changePage(int dir) {
  if (!layout || layout->pageCount == 0) return;
  page = (page + dir + layout->pageCount) % layout->pageCount;
  shortcutsShow(layout, page);
}

static void onInput(const InputEvent& ev) {
  uint32_t now = millis();
  lastActivity = now;

  if (mode == MODE_ALERT) {
    if (ev.type == IN_BTN_SHORT || ev.type == IN_BTN_LONG || ev.type == IN_TAP || ev.type == IN_LONG_TOUCH ||
        ev.type == IN_SWIPE_L || ev.type == IN_SWIPE_R || ev.type == IN_SWIPE_U || ev.type == IN_SWIPE_D) dismissAlert();
    return;
  }

  if (mode == MODE_SETTINGS) {
    if (ev.type == IN_ENC_CW) adjustBrightness(+1);
    else if (ev.type == IN_ENC_CCW) adjustBrightness(-1);
    else if (ev.type == IN_BTN_SHORT || ev.type == IN_BTN_LONG || ev.type == IN_TAP) closeSettings();
    return;
  }

  // swipe left/right moves around the carousel from every screen
  if (ev.type == IN_SWIPE_L) { navigate(+1); return; }
  if (ev.type == IN_SWIPE_R) { navigate(-1); return; }

  switch (mode) {
    case MODE_BUDDY:
      if (ev.type == IN_ENC_CW) buddyLook(1.f, 0.f, now);
      else if (ev.type == IN_ENC_CCW) buddyLook(-1.f, 0.f, now);
      else if (ev.type == IN_TAP || ev.type == IN_BTN_SHORT) gotoMode(MODE_CLOCK);      // tap shows the clock
      else if (ev.type == IN_BTN_LONG || ev.type == IN_LONG_TOUCH) openSettings();
      break;

    case MODE_CLOCK:
      if (ev.type == IN_TAP || ev.type == IN_BTN_SHORT) gotoMode(MODE_BUDDY);
      else if (ev.type == IN_ENC_CW) adjustBrightness(+1);
      else if (ev.type == IN_ENC_CCW) adjustBrightness(-1);
      else if (ev.type == IN_BTN_LONG || ev.type == IN_LONG_TOUCH) openSettings();
      break;

    case MODE_SHORTCUTS:
      if (ev.type == IN_ENC_CW) changePage(+1);
      else if (ev.type == IN_ENC_CCW) changePage(-1);
      else if (ev.type == IN_TAP) fireSlot(shortcutsHit(ev.x, ev.y));
      else if (ev.type == IN_BTN_SHORT) gotoMode(MODE_BUDDY);
      else if (ev.type == IN_BTN_LONG || ev.type == IN_LONG_TOUCH) openSettings();
      break;

    case MODE_TIMER:
      switch (ev.type) {
        case IN_ENC_CW:
        case IN_ENC_CCW:
          if (cd.state() == Countdown::IDLE) { cd.adjust(ev.type == IN_ENC_CW ? +1 : -1); customTimer = true; }
          break;
        case IN_BTN_SHORT:
        case IN_TAP:
          cd.toggle(now);
          saveTimer(now);
          break;
        case IN_BTN_LONG:
          cd.reset();
          saveTimer(now);
          break;
        case IN_SWIPE_U:
        case IN_SWIPE_D:
          if (cd.state() == Countdown::IDLE) selectPreset(presetIdx + (ev.type == IN_SWIPE_U ? 1 : -1));
          break;
        case IN_LONG_TOUCH: openSettings(); break;
        default: break;
      }
      break;

    case MODE_AGENDA:
      if (ev.type == IN_TAP || ev.type == IN_BTN_SHORT) { netCmd(NC_CAL_REFRESH); toast("Refreshing...", 1200); }
      else if (ev.type == IN_BTN_LONG || ev.type == IN_LONG_TOUCH) openSettings();
      break;

    case MODE_WEATHER:
      if (ev.type == IN_TAP || ev.type == IN_BTN_SHORT) { netCmd(NC_WX_REFRESH); toast("Refreshing...", 1200); }
      else if (ev.type == IN_BTN_LONG || ev.type == IN_LONG_TOUCH) openSettings();
      break;

    default: break;
  }
}

// ------------------------------------------------------------------ public
void uiInit() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setBrightness(gCfg.bright);
  appliedBright = gCfg.bright;

  lv_init();
  const uint32_t bufPx = SCREEN_W * 40;
  lvBuf = (lv_color_t*)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, bufPx);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  screensBuild();
  layout = layoutLoad();                       // NVS read: fine here, the tasks have not started yet
  selectPreset(0);
  buddyInit(tft);

  pendingTimer = settingsLoadTimer();
  hasPending = pendingTimer.state == Countdown::RUNNING || pendingTimer.state == Countdown::PAUSED;

  inputInit();
  strip.begin();
  strip.setBrightness(255);
  strip.clear();
  strip.show();

  lastActivity = millis();
  takeSnapshot();
  gotoMode(MODE_BUDDY);
}

void uiTask(void*) {
  uint32_t tRefresh = 0;
  for (;;) {
    UiMsg m;
    while (xQueueReceive(qToUi, &m, 0) == pdTRUE) handleMsg(m);

    InputEvent ev;
    while (inputPoll(ev)) onInput(ev);

    uint32_t now = millis();
    if (now - tRefresh >= 100) { tRefresh = now; refresh(now); }

    if (mode == MODE_BUDDY) {
      buddyTick(now);
      vTaskDelay(1);
    } else {
      shortcutsTick(now);
      lv_timer_handler();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}
