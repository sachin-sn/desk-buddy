#pragma once
// The clock faces the buddy can show, their names, and the rules for the accent colour. Pure (no Arduino,
// no LVGL), so the web editor's list, the saved setting and the tests all come from one place.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum FaceId : uint8_t {
  FACE_CLASSIC = 0,    // big digital time, seconds ring (the original clock)
  FACE_ANALOG,         // ticks, numerals, hour/minute hands and a gliding second hand
  FACE_MINIMAL,        // dots, thin hands, one dot orbiting for the seconds
  FACE_TACHO,          // a tachometer: the needle counts seconds, a rev-sweep plays on the hour
  FACE_ROLL,           // digits that roll over like a flip clock
  FACE_PHOTO,          // your own photo with the time on top
  FACE_COUNT
};

// key = thumbnail file name in the web editor (data/www/faces/<key>.png)
inline const char* faceKey(uint8_t id) {
  switch (id) {
    case FACE_CLASSIC: return "classic";
    case FACE_ANALOG:  return "analog";
    case FACE_MINIMAL: return "minimal";
    case FACE_TACHO:   return "tacho";
    case FACE_ROLL:    return "roll";
    case FACE_PHOTO:   return "photo";
  }
  return "classic";
}

inline const char* faceTitle(uint8_t id) {
  switch (id) {
    case FACE_CLASSIC: return "Classic";
    case FACE_ANALOG:  return "Analogue";
    case FACE_MINIMAL: return "Minimal";
    case FACE_TACHO:   return "Tacho";
    case FACE_ROLL:    return "Roll";
    case FACE_PHOTO:   return "Photo";
  }
  return "Classic";
}

inline const char* faceBlurb(uint8_t id) {
  switch (id) {
    case FACE_CLASSIC: return "Big digital time with a seconds ring, date, weather and your next meeting.";
    case FACE_ANALOG:  return "Hour, minute and gliding second hands over a dial with numerals.";
    case FACE_MINIMAL: return "Dots and thin hands. A single dot circles the dial once a minute.";
    case FACE_TACHO:   return "A tachometer. The needle counts seconds and the engine revs on the hour.";
    case FACE_ROLL:    return "Digits roll over like a flip clock whenever the time changes.";
    case FACE_PHOTO:   return "Your own photo with the time on top. One photo, cropped to fit.";
  }
  return "";
}

inline bool faceIdOk(int id) { return id >= 0 && id < FACE_COUNT; }
inline uint8_t faceClamp(int id) { return faceIdOk(id) ? (uint8_t)id : (uint8_t)FACE_CLASSIC; }

// ---- accent colour: seconds ring, second hand, needle, highlights --------------------------------------
#define ACCENT_DEFAULT 0x00D1FF

// "#RRGGBB" (exactly that shape) -> 0xRRGGBB
inline bool colorParse(const char* s, uint32_t& rgb) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  uint32_t v = 0;
  for (int i = 1; i < 7; i++) {
    char c = s[i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | d;
  }
  rgb = v;
  return true;
}

inline void colorFormat(uint32_t rgb, char* out /* at least 8 bytes */) {
  snprintf(out, 8, "#%02X%02X%02X", (unsigned)((rgb >> 16) & 255), (unsigned)((rgb >> 8) & 255), (unsigned)(rgb & 255));
}

// Every face is drawn on black, so an accent that is almost black would make the hands and ring vanish.
inline bool accentVisible(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  uint32_t m = r > g ? r : g;
  if (b > m) m = b;
  return m >= 0x60;
}

// ---- photo face -----------------------------------------------------------------------------------------
// How much the photo is darkened so the time stays readable, in percent.
#define PHOTO_DIM_DEFAULT 40
#define PHOTO_DIM_MAX     80
inline uint8_t photoDimClamp(int v) { return (uint8_t)(v < 0 ? 0 : v > PHOTO_DIM_MAX ? PHOTO_DIM_MAX : v); }
