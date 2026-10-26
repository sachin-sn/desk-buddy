// The clock faces (core 1, LVGL). One face exists at a time: choosing another one empties `root` and builds
// the new one. The arithmetic (angles, easing, colours) lives in facegeom.h / faceids.h and is unit-tested.
//
// Drawing notes for the hand-based faces (analogue, minimal, tacho): the dial is ONE transparent object that
// draws its ticks, hands and hub in a draw callback, and only the small rectangle around a hand that moved is
// invalidated. A lv_line per hand would invalidate everything from the screen corner to the hand's tip.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "facegeom.h"
#include "faceids.h"
#include "photostore.h"
#include "ui_internal.h"

using namespace facegeom;

namespace {

const int CX = 120, CY = 120;

// ------------------------------------------------------------------ small helpers
lv_obj_t* root = nullptr;           // holds the face that is showing
int       curFace = -1;             // -1: nothing built yet
uint32_t  curAccent = 0;
uint32_t  enteredMs = 0;

// A label that is only touched when its text changes (lv_label_set_text always invalidates).
struct Txt {
  lv_obj_t* obj = nullptr;
  char      cache[48] = "";
  void set(const char* t) {
    if (!obj) return;
    if (strlen(t) < sizeof cache && strcmp(cache, t) == 0) return;
    strlcpy(cache, t, sizeof cache);
    lv_label_set_text(obj, t);
  }
  void color(uint32_t rgb) { if (obj) lv_obj_set_style_text_color(obj, lv_color_hex(rgb), 0); }
};

lv_obj_t* plain(lv_obj_t* parent, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// fixed width + centred text: the label never needs re-aligning when its text changes
Txt makeText(lv_obj_t* parent, const lv_font_t* f, uint32_t color, int width, int dy) {
  Txt t;
  t.obj = newLabel(parent, f, color);
  lv_label_set_long_mode(t.obj, LV_LABEL_LONG_DOT);
  lv_obj_set_size(t.obj, width, lv_font_get_line_height(f));       // one line: longer text ends in "..." instead of wrapping
  lv_obj_align(t.obj, LV_ALIGN_CENTER, 0, dy);
  return t;
}

struct Lines {                       // what every face shows besides the time
  char time[8], date[20], wx[40], wxShort[12], dateShort[12];
  uint32_t wxColor;
};

void makeLines(const ClockView& v, Lines& l) {
  const Snapshot& s = *v.s;
  if (s.timeValid) strftime(l.time, sizeof l.time, "%H:%M", &s.t); else snprintf(l.time, sizeof l.time, "--:--");
  if (s.timeValid) strftime(l.date, sizeof l.date, "%a %d %b", &s.t); else snprintf(l.date, sizeof l.date, "waiting for time");
  if (s.timeValid) strftime(l.dateShort, sizeof l.dateShort, "%a %d", &s.t); else l.dateShort[0] = 0;
  l.wx[0] = 0;
  l.wxShort[0] = 0;
  l.wxColor = 0x9AA0A6;
  if (s.timeValid && weatherFresh(s.wx, s.now)) {      // "28°C Partly cloudy", coloured to hint at the weather
    snprintf(l.wx, sizeof l.wx, "%d" WX_DEG "%c %s", (int)s.wx.temp, s.wx.unit, weatherWords(s.wx.code, s.wx.isDay));
    snprintf(l.wxShort, sizeof l.wxShort, "%d" WX_DEG "%c", (int)s.wx.temp, s.wx.unit);
    l.wxColor = weatherColor(s.wx.code, s.wx.isDay);
  }
}

int secOf(const ClockView& v) { return v.s->timeValid ? v.s->t.tm_sec : 0; }

// ---- drawing inside a draw callback
void dLine(lv_draw_ctx_t* c, Pt a, Pt b, int w, uint32_t rgb) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(rgb);
  d.opa = LV_OPA_COVER;
  d.width = w;
  d.round_start = 1;
  d.round_end = 1;
  lv_point_t p1, p2;
  p1.x = a.x; p1.y = a.y; p2.x = b.x; p2.y = b.y;
  lv_draw_line(c, &d, &p1, &p2);
}

void dDot(lv_draw_ctx_t* c, int cx, int cy, int r, uint32_t rgb) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(rgb);
  d.bg_opa = LV_OPA_COVER;
  d.border_width = 0;
  d.radius = LV_RADIUS_CIRCLE;
  lv_area_t a;
  a.x1 = cx - r; a.y1 = cy - r; a.x2 = cx + r; a.y2 = cy + r;
  lv_draw_rect(c, &d, &a);
}

void dirtySeg(lv_obj_t* o, Pt a, Pt b, int pad) {
  lv_area_t r;
  r.x1 = (a.x < b.x ? a.x : b.x) - pad; r.y1 = (a.y < b.y ? a.y : b.y) - pad;
  r.x2 = (a.x > b.x ? a.x : b.x) + pad; r.y2 = (a.y > b.y ? a.y : b.y) + pad;
  lv_obj_invalidate_area(o, &r);
}

void dirtyDot(lv_obj_t* o, int cx, int cy, int r) {
  lv_area_t a;
  a.x1 = cx - r - 1; a.y1 = cy - r - 1; a.x2 = cx + r + 1; a.y2 = cy + r + 1;
  lv_obj_invalidate_area(o, &a);
}

// a hand: its angle, and where it was last drawn
struct Hand {
  float ang = -1000.f;
  float len = 0, tail = 0;
  int   w = 4;
  uint32_t color = 0xFFFFFF;
  Pt tip() const { return polar(CX, CY, len, ang); }
  Pt end() const { return polar(CX, CY, -tail, ang); }
  void draw(lv_draw_ctx_t* c) const { if (ang > -999.f) dLine(c, end(), tip(), w, color); }
  // moves the hand and invalidates the old and the new position; returns true if it moved
  bool set(lv_obj_t* dial, float a, float eps = 0.04f) {
    if (ang > -999.f && fabsf(a - ang) < eps) return false;
    if (ang > -999.f) dirtySeg(dial, end(), tip(), w / 2 + 2);
    ang = a;
    dirtySeg(dial, end(), tip(), w / 2 + 2);
    return true;
  }
};

// =================================================================== 0: classic
struct Classic {
  lv_obj_t* ring = nullptr;
  Txt time, date, next, wx;
} classic;

void classicBuild(const ClockView& v) {
  classic.ring = newRing(root, 114, 5, v.accent, 0x14181C, 60);
  classic.time = makeText(root, &lv_font_montserrat_48, 0xFFFFFF, 190, -14);
  classic.date = makeText(root, &lv_font_montserrat_20, 0x9AA0A6, 190, 30);
  classic.next = makeText(root, &lv_font_montserrat_14, v.accent, 170, 64);
  classic.wx   = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 160, -66);
}

void classicUpdate(const ClockView& v) {
  Lines l;
  makeLines(v, l);
  classic.time.set(l.time);
  classic.date.set(l.date);
  int sec = secOf(v);
  if (lv_arc_get_value(classic.ring) != sec) lv_arc_set_value(classic.ring, sec);
  classic.next.color(v.nextColor);
  classic.next.set(v.nextLine);
  classic.wx.color(l.wxColor);
  classic.wx.set(l.wx);
}

// =================================================================== 1: analogue, 2: minimal
struct Dial {
  lv_obj_t*     obj = nullptr;
  bool          minimal = false;
  uint32_t      accent = 0;
  Hand          hour, min, sec;
  SecondTracker st;
  float         orbit = -1000.f;       // minimal: where the seconds dot is
  Txt           date, next, wx;
} dial;

void analogDraw(lv_draw_ctx_t* c) {
  // ticks
  for (int i = 0; i < 60; i++) {
    float a = (float)i * 6.f;
    if (i % 5 == 0) {
      bool twelve = i == 0;
      dLine(c, polar(CX, CY, 99, a), polar(CX, CY, 113, a), twelve ? 5 : 3, twelve ? dial.accent : 0xE6E8EB);
    } else {
      dLine(c, polar(CX, CY, 108, a), polar(CX, CY, 113, a), 1, 0x5A626C);
    }
  }
  dial.hour.draw(c);
  dial.min.draw(c);
  dial.sec.draw(c);
  dDot(c, CX, CY, 7, dial.accent);
  dDot(c, CX, CY, 3, 0x000000);
}

void minimalDraw(lv_draw_ctx_t* c) {
  for (int i = 0; i < 12; i++) {
    float a = (float)i * 30.f;
    Pt p = polar(CX, CY, 109, a);
    if (i == 0) dDot(c, p.x, p.y, 4, 0xF2F4F7);
    else if (i % 3 == 0) dDot(c, p.x, p.y, 3, 0xE6E8EB);
    else dDot(c, p.x, p.y, 2, 0x5A626C);
  }
  dial.hour.draw(c);
  dial.min.draw(c);
  dDot(c, CX, CY, 5, 0xF2F4F7);
  if (dial.orbit > -999.f) {
    Pt o = polar(CX, CY, 94, dial.orbit);
    dDot(c, o.x, o.y, 5, dial.accent);
  }
}

void dialEvent(lv_event_t* e) {
  lv_draw_ctx_t* c = lv_event_get_draw_ctx(e);
  if (dial.minimal) minimalDraw(c); else analogDraw(c);
}

void dialBuild(const ClockView& v, bool minimal) {
  dial = Dial();
  dial.minimal = minimal;
  dial.accent = v.accent;
  if (!minimal) {
    // numerals and text go first so the hands pass over them
    const lv_font_t* nf = &lv_font_montserrat_28;
    const char* num[4] = {"12", "3", "6", "9"};
    for (int i = 0; i < 4; i++) {
      Pt p = polar(CX, CY, 78, (float)i * 90.f);
      lv_obj_t* l = newLabel(root, nf, 0xE6E8EB);
      lv_label_set_text(l, num[i]);
      lv_obj_align(l, LV_ALIGN_CENTER, p.x - CX, p.y - CY);
    }
    dial.wx   = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 130, -38);
    dial.date = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 110, 28);
    dial.next = makeText(root, &lv_font_montserrat_14, v.accent, 140, 48);
    dial.hour.len = 56; dial.hour.w = 7; dial.hour.color = 0xF2F4F7; dial.hour.tail = 0;
    dial.min.len = 88;  dial.min.w = 5;  dial.min.color = 0xF2F4F7;  dial.min.tail = 0;
    dial.sec.len = 100; dial.sec.w = 2;  dial.sec.color = v.accent;  dial.sec.tail = 22;
  } else {
    dial.date = makeText(root, &lv_font_montserrat_14, 0x6B737D, 100, 40);
    dial.hour.len = 52; dial.hour.w = 6; dial.hour.color = 0xF2F4F7; dial.hour.tail = 0;
    dial.min.len = 86;  dial.min.w = 4;  dial.min.color = 0xF2F4F7;  dial.min.tail = 0;
  }
  dial.obj = plain(root, 240, 240);
  lv_obj_add_event_cb(dial.obj, dialEvent, LV_EVENT_DRAW_MAIN_END, NULL);
}

void dialUpdate(const ClockView& v) {
  const Snapshot& s = *v.s;
  int h = s.timeValid ? s.t.tm_hour : 10, m = s.timeValid ? s.t.tm_min : 10;
  float sec = (float)secOf(v) + (s.timeValid ? dial.st.frac(secOf(v), v.nowMs) : 0.f);
  dial.hour.set(dial.obj, hourAngle(h, m, sec));
  dial.min.set(dial.obj, minuteAngle(m, sec));
  if (!dial.minimal) {
    dial.sec.set(dial.obj, s.timeValid ? secondAngle(sec) : 0.f, 0.3f);
  } else {
    float a = s.timeValid ? secondAngle(sec) : 0.f;
    if (dial.orbit < -999.f || fabsf(a - dial.orbit) >= 0.3f) {
      if (dial.orbit > -999.f) { Pt o = polar(CX, CY, 94, dial.orbit); dirtyDot(dial.obj, o.x, o.y, 5); }
      dial.orbit = a;
      Pt o = polar(CX, CY, 94, dial.orbit);
      dirtyDot(dial.obj, o.x, o.y, 5);
    }
  }
  Lines l;
  makeLines(v, l);
  dial.date.set(l.dateShort);
  if (!dial.minimal) {
    dial.next.color(v.nextColor);
    dial.next.set(v.nextLine);
    dial.wx.color(l.wxColor);
    dial.wx.set(l.wxShort);                      // just the temperature: the dial has no room for words
  }
}

// =================================================================== 3: tacho
struct Tacho {
  lv_obj_t*     obj = nullptr;
  uint32_t      accent = 0;
  Hand          needle;
  SecondTracker st;
  uint32_t      revStart = 0;
  bool          revving = false;
  int           revHour = -1;
  Txt           time, date, next, wx;
} tacho;

const uint32_t REDLINE = 0xE5372E;

lv_obj_t* tachoArc(int startDeg, int endDeg, int w, uint32_t color) {
  lv_obj_t* a = lv_arc_create(root);
  lv_obj_set_size(a, 224, 224);
  lv_obj_center(a);
  lv_arc_set_bg_angles(a, startDeg, endDeg);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_remove_style(a, NULL, LV_PART_INDICATOR);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
  return a;
}

void tachoDraw(lv_draw_ctx_t* c) {
  for (int i = 0; i <= 60; i++) {
    float a = tachoAngle((float)i / 60.f);
    bool red = i > 50;
    if (i % 10 == 0)      dLine(c, polar(CX, CY, 84, a), polar(CX, CY, 99, a), 3, red ? REDLINE : 0xF2F4F7);
    else if (i % 5 == 0)  dLine(c, polar(CX, CY, 90, a), polar(CX, CY, 99, a), 2, red ? REDLINE : 0xB5BBC3);
    else                  dLine(c, polar(CX, CY, 94, a), polar(CX, CY, 99, a), 1, red ? 0x8A2822 : 0x5A626C);
  }
  tacho.needle.draw(c);
  dDot(c, CX, CY, 10, 0x2A3038);
  dDot(c, CX, CY, 4, tacho.accent);
}

void tachoEvent(lv_event_t* e) { tachoDraw(lv_event_get_draw_ctx(e)); }

void tachoBuild(const ClockView& v) {
  tacho = Tacho();
  tacho.accent = v.accent;
  tachoArc(135, 45, 8, 0x1B2026);              // the dial: 270 degrees, bottom left round to bottom right
  tachoArc(0, 45, 8, REDLINE);                 // the last ten seconds are the red zone
  for (int s = 0; s <= 60; s += 10) {
    Pt p = polar(CX, CY, 68, tachoAngle((float)s / 60.f));
    lv_obj_t* l = newLabel(root, &lv_font_montserrat_14, s > 50 ? REDLINE : 0xC9CDD3);
    char b[4];
    snprintf(b, sizeof b, "%d", s);
    lv_label_set_text(l, b);
    lv_obj_align(l, LV_ALIGN_CENTER, p.x - CX, p.y - CY);
  }
  tacho.wx   = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 70, -32);
  tacho.next = makeText(root, &lv_font_montserrat_14, v.accent, 140, 30);
  tacho.time = makeText(root, &lv_font_montserrat_28, 0xFFFFFF, 100, 70);
  tacho.date = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 110, 94);
  tacho.needle.len = 92; tacho.needle.tail = 16; tacho.needle.w = 4; tacho.needle.color = v.accent;
  tacho.obj = plain(root, 240, 240);
  lv_obj_add_event_cb(tacho.obj, tachoEvent, LV_EVENT_DRAW_MAIN_END, NULL);
  tacho.revStart = enteredMs;                  // the engine revs once when the face appears
  tacho.revving = true;
}

void tachoUpdate(const ClockView& v) {
  const Snapshot& s = *v.s;
  // a rev on the hour: at hh:00:00 for the first two seconds
  if (s.timeValid && s.t.tm_min == 0 && s.t.tm_sec < 2 && tacho.revHour != s.t.tm_hour) {
    tacho.revHour = s.t.tm_hour;
    tacho.revStart = v.nowMs;
    tacho.revving = true;
  }
  float value = s.timeValid ? ((float)secOf(v) + tacho.st.frac(secOf(v), v.nowMs)) / 60.f : 0.f;
  if (tacho.revving) {
    float r = revSweep(v.nowMs - tacho.revStart);
    if (r < 0.f) tacho.revving = false;
    else if (r > value) value = r;
  }
  tacho.needle.set(tacho.obj, tachoAngle(value), 0.3f);

  Lines l;
  makeLines(v, l);
  tacho.time.set(l.time);
  tacho.date.set(l.dateShort);
  tacho.next.color(v.nextColor);
  tacho.next.set(v.nextLine);
  tacho.wx.color(l.wxColor);
  tacho.wx.set(l.wxShort);
}

// =================================================================== 4: roll
struct Card {
  lv_obj_t *box = nullptr, *cur = nullptr, *nxt = nullptr;
  char      shown = 0;
  uint32_t  start = 0;
  bool      rolling = false;
};
struct Roll {
  lv_obj_t* ring = nullptr;
  Card      card[4];
  lv_obj_t* colon = nullptr;
  bool      first = true;
  int       lastSec = -1;
  Txt       date, next, wx;
} roll;

const int CARD_W = 44, CARD_H = 64, CARD_GAP = 6;

int cardTextY() { return (CARD_H - (int)lv_font_get_line_height(&lv_font_montserrat_48)) / 2; }

void cardSet(Card& k, char ch, bool animate, uint32_t now) {
  if (ch == k.shown) return;                    // already showing (or rolling towards) this digit
  char b[2] = {ch, 0};
  if (!animate || k.shown == 0) {
    k.shown = ch;
    k.rolling = false;
    lv_label_set_text(k.cur, b);
    lv_obj_set_y(k.cur, cardTextY());
    lv_obj_set_y(k.nxt, cardTextY() + CARD_H);
    return;
  }
  if (k.rolling) {                              // changed again mid-roll: finish the old roll at once
    char o[2] = {k.shown, 0};
    lv_label_set_text(k.cur, o);
  }
  lv_label_set_text(k.nxt, b);
  k.shown = ch;
  k.start = now;
  k.rolling = true;
}

void cardStep(Card& k, uint32_t now) {
  if (!k.rolling) return;
  uint32_t el = now - k.start;
  if (el >= ROLL_MS) {
    k.rolling = false;
    lv_label_set_text(k.cur, lv_label_get_text(k.nxt));
    lv_obj_set_y(k.cur, cardTextY());
    lv_obj_set_y(k.nxt, cardTextY() + CARD_H);
    return;
  }
  int off = rollOffset(el, CARD_H);
  lv_obj_set_y(k.cur, cardTextY() - off);
  lv_obj_set_y(k.nxt, cardTextY() + CARD_H - off);
}

void rollBuild(const ClockView& v) {
  roll = Roll();
  roll.ring = newRing(root, 114, 4, v.accent, 0x14181C, 60);
  roll.wx   = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 160, -62);
  const int top = CY - CARD_H / 2;
  const int xs[4] = {CX - 7 - 2 * CARD_W - CARD_GAP, CX - 7 - CARD_W, CX + 7, CX + 7 + CARD_W + CARD_GAP};
  for (int i = 0; i < 4; i++) {
    Card& k = roll.card[i];
    k.box = lv_obj_create(root);
    lv_obj_set_size(k.box, CARD_W, CARD_H);
    lv_obj_set_pos(k.box, xs[i], top);
    lv_obj_set_style_bg_color(k.box, lv_color_hex(0x15191E), 0);
    lv_obj_set_style_bg_opa(k.box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k.box, 1, 0);
    lv_obj_set_style_border_color(k.box, lv_color_hex(0x2A313A), 0);
    lv_obj_set_style_radius(k.box, 10, 0);
    lv_obj_set_style_pad_all(k.box, 0, 0);
    lv_obj_clear_flag(k.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(k.box, LV_OBJ_FLAG_CLICKABLE);
    k.cur = newLabel(k.box, &lv_font_montserrat_48, 0xFFFFFF);
    k.nxt = newLabel(k.box, &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_width(k.cur, CARD_W); lv_obj_set_width(k.nxt, CARD_W);
    lv_obj_set_pos(k.cur, 0, cardTextY());
    lv_obj_set_pos(k.nxt, 0, cardTextY() + CARD_H);
  }
  roll.colon = newLabel(root, &lv_font_montserrat_48, v.accent);
  lv_label_set_text(roll.colon, ":");
  lv_obj_align(roll.colon, LV_ALIGN_CENTER, 0, -3);
  roll.date = makeText(root, &lv_font_montserrat_20, 0x9AA0A6, 190, 58);
  roll.next = makeText(root, &lv_font_montserrat_14, v.accent, 130, 84);
}

void rollUpdate(const ClockView& v) {
  const Snapshot& s = *v.s;
  char d[4];
  timeDigits(s.timeValid, s.t.tm_hour, s.t.tm_min, d);
  for (int i = 0; i < 4; i++) { cardSet(roll.card[i], d[i], !roll.first, v.nowMs); cardStep(roll.card[i], v.nowMs); }
  roll.first = false;

  int sec = secOf(v);
  if (lv_arc_get_value(roll.ring) != sec) lv_arc_set_value(roll.ring, sec);
  if (sec != roll.lastSec) {                    // the colon pulses once a second
    roll.lastSec = sec;
    lv_obj_set_style_text_opa(roll.colon, (sec & 1) ? LV_OPA_30 : LV_OPA_COVER, 0);
  }
  Lines l;
  makeLines(v, l);
  roll.date.set(l.date);
  roll.next.color(v.nextColor);
  roll.next.set(v.nextLine);
  roll.wx.color(l.wxColor);
  roll.wx.set(l.wx);
}

// =================================================================== 5: photo
struct Photo {
  lv_obj_t* img = nullptr;
  lv_obj_t* dim = nullptr;
  lv_obj_t* ring = nullptr;
  uint32_t  shownVer = 0xFFFFFFFFu;
  uint8_t   shownDim = 255;
  Txt       time, timeShadow, date, dateShadow, next, wx, hint;
} photo;

lv_img_dsc_t photoDsc;                           // describes the PSRAM picture to LVGL

void photoPoint(const PhotoRef& p) {
  memset(&photoDsc, 0, sizeof photoDsc);
  photoDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  photoDsc.header.w = PHOTO_W;
  photoDsc.header.h = PHOTO_H;
  photoDsc.data_size = PHOTO_PIXEL_BYTES;
  photoDsc.data = (const uint8_t*)p.px;
}

void photoBuild(const ClockView& v) {
  photo = Photo();
  PhotoRef p = photoCurrent();
  photoPoint(p);
  photoSeen(p.ver);
  photo.img = lv_img_create(root);
  lv_obj_set_pos(photo.img, 0, 0);
  lv_obj_clear_flag(photo.img, LV_OBJ_FLAG_CLICKABLE);
  if (p.px) {
    lv_img_set_src(photo.img, &photoDsc);
  } else {
    lv_obj_add_flag(photo.img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);                  // no photo yet: a dusky gradient instead
    lv_obj_set_style_bg_color(root, lv_color_hex(0x16243A), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
  }
  photo.shownVer = p.ver;
  photo.dim = plain(root, 240, 240);
  lv_obj_set_style_bg_color(photo.dim, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(photo.dim, (lv_opa_t)(v.photoDim * 255 / 100), 0);
  photo.shownDim = v.photoDim;

  photo.ring = newRing(root, 116, 3, v.accent, 0x14181C, 60);
  lv_obj_set_style_arc_opa(photo.ring, LV_OPA_TRANSP, LV_PART_MAIN);   // only the moving part, no track
  photo.wx = makeText(root, &lv_font_montserrat_14, 0xFFFFFF, 160, -66);
  photo.timeShadow = makeText(root, &lv_font_montserrat_48, 0x000000, 190, -12);
  lv_obj_set_style_text_opa(photo.timeShadow.obj, LV_OPA_60, 0);
  photo.time = makeText(root, &lv_font_montserrat_48, 0xFFFFFF, 190, -14);
  photo.dateShadow = makeText(root, &lv_font_montserrat_20, 0x000000, 190, 32);
  lv_obj_set_style_text_opa(photo.dateShadow.obj, LV_OPA_60, 0);
  photo.date = makeText(root, &lv_font_montserrat_20, 0xE6E8EB, 190, 30);
  photo.next = makeText(root, &lv_font_montserrat_14, v.accent, 170, 64);
  if (!p.px) {
    photo.hint = makeText(root, &lv_font_montserrat_14, 0x9AA0A6, 120, 92);
    photo.hint.set("no photo yet");
  }
}

void photoUpdate(const ClockView& v) {
  PhotoRef p = photoCurrent();
  if (p.ver != photo.shownVer) {                // a new photo was uploaded (or removed): rebuild this face
    curFace = -1;
    return;
  }
  if (photo.shownDim != v.photoDim) {
    photo.shownDim = v.photoDim;
    lv_obj_set_style_bg_opa(photo.dim, (lv_opa_t)(v.photoDim * 255 / 100), 0);
  }
  Lines l;
  makeLines(v, l);
  photo.time.set(l.time);
  photo.timeShadow.set(l.time);
  photo.date.set(l.date);
  photo.dateShadow.set(l.date);
  int sec = secOf(v);
  if (lv_arc_get_value(photo.ring) != sec) lv_arc_set_value(photo.ring, sec);
  photo.next.color(v.nextColor);
  photo.next.set(v.nextLine);
  photo.wx.set(l.wx);
}

// =================================================================== registry
struct FaceOps {
  void (*build)(const ClockView&);
  void (*update)(const ClockView&);
};
void analogBuild(const ClockView& v) { dialBuild(v, false); }
void minimalBuild(const ClockView& v) { dialBuild(v, true); }

const FaceOps FACES[FACE_COUNT] = {
  {classicBuild, classicUpdate},    // FACE_CLASSIC
  {analogBuild,  dialUpdate},       // FACE_ANALOG
  {minimalBuild, dialUpdate},       // FACE_MINIMAL
  {tachoBuild,   tachoUpdate},      // FACE_TACHO
  {rollBuild,    rollUpdate},       // FACE_ROLL
  {photoBuild,   photoUpdate},      // FACE_PHOTO
};

void clearRoot() {
  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  classic = Classic(); dial = Dial(); tacho = Tacho(); roll = Roll(); photo = Photo();
}

}   // namespace

// ------------------------------------------------------------------ public
void facesInit(lv_obj_t* screen) {
  root = plain(screen, 240, 240);
}

void clockEnter(uint32_t nowMs) {
  enteredMs = nowMs;
  if (curFace == FACE_TACHO) { tacho.revStart = nowMs; tacho.revving = true; }
  if (curFace == FACE_ROLL) roll.first = true;       // show the digits at once, no roll on entry
  if (curFace == FACE_PHOTO && photoCurrent().ver != photo.shownVer) curFace = -1;   // rebuild before it is drawn
}

void clockPhotoSync() {
  photoSeen(photoCurrent().ver);                     // no photo is on screen: the older buffer may be reused
}

void clockRefresh(const ClockView& v) {
  if (!root) return;
  uint8_t f = faceClamp(v.face);
  for (int pass = 0; pass < 2; pass++) {             // the second pass only happens when a face asks for a rebuild
    if ((int)f != curFace || v.accent != curAccent) {
      clearRoot();
      curFace = f;
      curAccent = v.accent;
      FACES[f].build(v);
    }
    FACES[f].update(v);
    if (curFace != -1) break;
  }
  if (curFace != FACE_PHOTO) photoSeen(photoCurrent().ver);
}
