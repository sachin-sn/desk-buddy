#pragma once
// Parses shortcut strings into key steps. No Arduino dependencies (host-tested).
//
//   "cmd+shift+4"                      one key combo
//   "playpause"                        a media key (play, next, prev, volup, voldown, mute, stop)
//   "cmd+space;wait:350;text:Terminal;enter"   several steps, separated by ';'
//   "spot:Visual Studio Code"          sugar for opening an app through Spotlight
//
// Key codes are the ones used by the "ESP32 BLE Keyboard" library; hid.cpp has
// static_asserts that fail the build if they ever disagree with the library.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// Modifiers and special keys (BleKeyboard numbering). Letters/digits/punctuation are plain ASCII.
enum KeyCode : uint8_t {
  KC_LCTRL = 0x80, KC_LSHIFT = 0x81, KC_LALT = 0x82, KC_LGUI = 0x83,
  KC_UP = 0xDA, KC_DOWN = 0xD9, KC_LEFT = 0xD8, KC_RIGHT = 0xD7,
  KC_BACKSPACE = 0xB2, KC_TAB = 0xB3, KC_RETURN = 0xB0, KC_ESC = 0xB1,
  KC_INSERT = 0xD1, KC_DELETE = 0xD4, KC_PGUP = 0xD3, KC_PGDN = 0xD6,
  KC_HOME = 0xD2, KC_END = 0xD5, KC_CAPS = 0xC1,
  KC_F1 = 0xC2,    // F1..F12 are consecutive: 0xC2..0xCD
  KC_F13 = 0xF0    // F13..F24 are consecutive: 0xF0..0xFB
};

enum MediaId : uint8_t { MEDIA_NONE = 0, MEDIA_NEXT, MEDIA_PREV, MEDIA_STOP, MEDIA_PLAY_PAUSE, MEDIA_MUTE, MEDIA_VOL_UP, MEDIA_VOL_DOWN };

enum StepKind : uint8_t { ST_COMBO, ST_TEXT, ST_WAIT, ST_MEDIA };

struct Step {
  StepKind kind;
  uint8_t  nkeys;
  uint8_t  keys[6];      // ST_COMBO
  uint8_t  media;        // ST_MEDIA (MediaId)
  uint16_t ms;           // ST_WAIT
  char     text[48];     // ST_TEXT
};

#define MAX_STEPS 8

namespace kc_detail {

struct Named { const char* name; uint8_t code; };

static const Named kKeys[] = {
  {"enter", KC_RETURN}, {"return", KC_RETURN}, {"esc", KC_ESC}, {"escape", KC_ESC}, {"tab", KC_TAB},
  {"space", ' '}, {"backspace", KC_BACKSPACE}, {"bksp", KC_BACKSPACE}, {"delete", KC_DELETE}, {"del", KC_DELETE},
  {"up", KC_UP}, {"down", KC_DOWN}, {"left", KC_LEFT}, {"right", KC_RIGHT},
  {"home", KC_HOME}, {"end", KC_END}, {"pageup", KC_PGUP}, {"pagedown", KC_PGDN},
  {"insert", KC_INSERT}, {"capslock", KC_CAPS}, {"plus", '+'}, {"comma", ','},
};
static const Named kMods[] = {
  {"cmd", KC_LGUI}, {"command", KC_LGUI}, {"gui", KC_LGUI}, {"win", KC_LGUI}, {"super", KC_LGUI}, {"meta", KC_LGUI},
  {"ctrl", KC_LCTRL}, {"control", KC_LCTRL},
  {"alt", KC_LALT}, {"opt", KC_LALT}, {"option", KC_LALT},
  {"shift", KC_LSHIFT},
};
struct NamedMedia { const char* name; MediaId id; };
static const NamedMedia kMedia[] = {
  {"playpause", MEDIA_PLAY_PAUSE}, {"play", MEDIA_PLAY_PAUSE}, {"pause", MEDIA_PLAY_PAUSE},
  {"next", MEDIA_NEXT}, {"prev", MEDIA_PREV}, {"previous", MEDIA_PREV},
  {"volup", MEDIA_VOL_UP}, {"voldown", MEDIA_VOL_DOWN}, {"mute", MEDIA_MUTE}, {"stop", MEDIA_STOP},
};

inline bool ieq(const char* a, const char* b) {
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
inline bool ipre(const char* s, const char* prefix) {
  while (*prefix) { if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false; s++; prefix++; }
  return true;
}
inline void setErr(char* err, size_t n, const char* fmt, const char* a = "") {
  if (err && n) snprintf(err, n, fmt, a);
}
inline char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

// One token -> key code. Returns 0 if unknown.
inline uint8_t keyFor(const char* t, bool* isMod) {
  *isMod = false;
  for (const Named& m : kMods) if (ieq(t, m.name)) { *isMod = true; return m.code; }
  for (const Named& k : kKeys) if (ieq(t, k.name)) return k.code;
  size_t n = strlen(t);
  if ((t[0] == 'f' || t[0] == 'F') && n >= 2 && n <= 3) {           // f1..f24
    char* end = nullptr;
    long v = strtol(t + 1, &end, 10);
    if (end && *end == 0) {
      if (v >= 1 && v <= 12) return (uint8_t)(KC_F1 + (v - 1));
      if (v >= 13 && v <= 24) return (uint8_t)(KC_F13 + (v - 13));
    }
  }
  if (n == 1) {                                                      // single printable character
    char c = (char)tolower((unsigned char)t[0]);
    if (isalnum((unsigned char)c) || strchr("-=[]'`\\./", c)) return (uint8_t)c;
  }
  return 0;
}

inline bool parseCombo(char* seg, Step& st, char* err, size_t errLen) {
  // media key on its own?
  for (const NamedMedia& m : kMedia) {
    if (ieq(seg, m.name)) { st.kind = ST_MEDIA; st.media = m.id; return true; }
  }
  st.kind = ST_COMBO; st.nkeys = 0;
  char* p = seg;
  for (;;) {
    char* plus = strchr(p, '+');
    // a lone '+' key is written "plus"; a trailing/leading '+' is a syntax error
    if (plus) *plus = 0;
    char* tok = trim(p);
    if (!*tok) { setErr(err, errLen, "empty key in combo"); return false; }
    bool isMod = false;
    uint8_t code = keyFor(tok, &isMod);
    if (!code) { setErr(err, errLen, "unknown key '%s'", tok); return false; }
    if (st.nkeys >= sizeof(st.keys)) { setErr(err, errLen, "too many keys in one combo"); return false; }
    st.keys[st.nkeys++] = code;
    if (!plus) break;
    p = plus + 1;
  }
  return true;
}

}  // namespace kc_detail

// Returns the number of steps written (>0), or 0 on error (err explains).
inline int parseSequence(const char* s, Step* out, int maxSteps, char* err = nullptr, size_t errLen = 0) {
  using namespace kc_detail;
  if (err && errLen) err[0] = 0;
  if (!s || !*s) { setErr(err, errLen, "empty shortcut"); return 0; }
  char buf[96];
  if (strlen(s) >= sizeof(buf)) { setErr(err, errLen, "shortcut too long"); return 0; }
  strcpy(buf, s);

  int n = 0;
  char* save = buf;
  while (save) {
    char* semi = strchr(save, ';');
    if (semi) *semi = 0;
    char* seg = trim(save);
    save = semi ? semi + 1 : nullptr;
    if (!*seg) continue;
    if (n >= maxSteps) { setErr(err, errLen, "too many steps"); return 0; }
    Step& st = out[n];
    memset(&st, 0, sizeof(st));

    if (ipre(seg, "wait:")) {
      long ms = strtol(seg + 5, nullptr, 10);
      if (ms < 10) ms = 10;
      if (ms > 1500) ms = 1500;
      st.kind = ST_WAIT; st.ms = (uint16_t)ms; n++;
    } else if (ipre(seg, "text:")) {
      const char* t = seg + 5;
      while (*t == ' ') t++;
      if (!*t) { setErr(err, errLen, "text: needs some text"); return 0; }
      if (strlen(t) >= sizeof(st.text)) { setErr(err, errLen, "text is too long"); return 0; }
      st.kind = ST_TEXT; strcpy(st.text, t); n++;
    } else if (ipre(seg, "spot:")) {
      const char* app = seg + 5;
      while (*app == ' ') app++;
      if (!*app) { setErr(err, errLen, "spot: needs an app name"); return 0; }
      if (strlen(app) >= sizeof(st.text)) { setErr(err, errLen, "app name is too long"); return 0; }
      if (n + 5 > maxSteps) { setErr(err, errLen, "too many steps"); return 0; }
      // Cmd+Space, wait for Spotlight to open, type the name, wait for the result, Enter
      st.kind = ST_COMBO; st.keys[0] = KC_LGUI; st.keys[1] = ' '; st.nkeys = 2; n++;
      Step& w1 = out[n]; memset(&w1, 0, sizeof(w1)); w1.kind = ST_WAIT; w1.ms = 350; n++;
      Step& tx = out[n]; memset(&tx, 0, sizeof(tx)); tx.kind = ST_TEXT; strcpy(tx.text, app); n++;
      Step& w2 = out[n]; memset(&w2, 0, sizeof(w2)); w2.kind = ST_WAIT; w2.ms = 300; n++;
      Step& en = out[n]; memset(&en, 0, sizeof(en)); en.kind = ST_COMBO; en.keys[0] = KC_RETURN; en.nkeys = 1; n++;
    } else {
      if (!parseCombo(seg, st, err, errLen)) return 0;
      n++;
    }
  }
  if (n == 0) { setErr(err, errLen, "empty shortcut"); return 0; }
  return n;
}

// Total time the waits in a sequence will take (the caller caps this).
inline uint32_t sequenceWaitMs(const Step* s, int n) {
  uint32_t t = 0;
  for (int i = 0; i < n; i++) if (s[i].kind == ST_WAIT) t += s[i].ms;
  return t;
}
