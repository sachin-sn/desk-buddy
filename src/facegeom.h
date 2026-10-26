#pragma once
// The arithmetic behind the clock faces: where the hands point, how the seconds glide, how the tacho
// needle moves and how the digits roll. Pure (no Arduino, no LVGL) so it is unit-tested on a PC.
//
// Angles are in degrees, clockwise from 12 o'clock, unless a comment says otherwise.
#include <math.h>
#include <stdint.h>

namespace facegeom {

struct Pt { int x, y; };

inline float wrap360(float d) {
  d = fmodf(d, 360.f);
  return d < 0 ? d + 360.f : d;
}

// point at distance r from (cx, cy) in the direction `deg`
inline Pt polar(int cx, int cy, float r, float deg) {
  float a = deg * (float)M_PI / 180.f;
  Pt p;
  p.x = cx + (int)lroundf(sinf(a) * r);
  p.y = cy - (int)lroundf(cosf(a) * r);
  return p;
}

// `s` is the second including its fraction (0 <= s < 60)
inline float secondAngle(float s) { return s * 6.f; }
inline float minuteAngle(int m, float s) { return ((float)m + s / 60.f) * 6.f; }
inline float hourAngle(int h, int m, float s) { return ((float)(h % 12) + ((float)m + s / 60.f) / 60.f) * 30.f; }

// The clock only tells us whole seconds. To make a hand glide, remember when the second last changed
// and add the time since then (never reaching the next second, in case a tick comes late).
struct SecondTracker {
  int      lastSec = -1;
  uint32_t changedAt = 0;
  float frac(int sec, uint32_t nowMs) {
    if (sec != lastSec) { lastSec = sec; changedAt = nowMs; }
    float f = (float)(uint32_t)(nowMs - changedAt) / 1000.f;
    return f > 0.999f ? 0.999f : f;
  }
};

// ---- tacho ------------------------------------------------------------------------------------------------
// The dial covers 270 degrees and leaves the bottom quarter free for the digital time.
// 0 is at the bottom left (225 degrees clockwise from 12) and the end of the scale at the bottom right.
const float TACHO_START = 225.f;
const float TACHO_SWEEP = 270.f;
inline float tachoAngle(float v01) {                  // v01 in 0..1 -> clock degrees
  if (v01 < 0.f) v01 = 0.f;
  if (v01 > 1.f) v01 = 1.f;
  return wrap360(TACHO_START + TACHO_SWEEP * v01);
}

// The "engine rev": 0 -> 1 quickly, a short hold at the limiter, then back down. Returns -1 when over.
const uint32_t REV_UP_MS = 520, REV_HOLD_MS = 160, REV_DOWN_MS = 760;
const uint32_t REV_TOTAL_MS = REV_UP_MS + REV_HOLD_MS + REV_DOWN_MS;
inline float revSweep(uint32_t t) {
  if (t >= REV_TOTAL_MS) return -1.f;
  if (t < REV_UP_MS) {
    float x = (float)t / (float)REV_UP_MS;
    float inv = 1.f - x;
    return 1.f - inv * inv * inv;                      // ease-out: jumps off the line, settles at the limiter
  }
  if (t < REV_UP_MS + REV_HOLD_MS) return 1.f;
  float x = (float)(t - REV_UP_MS - REV_HOLD_MS) / (float)REV_DOWN_MS;
  return 1.f - x * x * (3.f - 2.f * x);                // smoothstep down
}

// ---- roll (flip-clock style digit change) ------------------------------------------------------------------
const uint32_t ROLL_MS = 320;
inline float easeOutCubic(float x) {
  if (x <= 0.f) return 0.f;
  if (x >= 1.f) return 1.f;
  float inv = 1.f - x;
  return 1.f - inv * inv * inv;
}
// how far the old digit has moved up, in pixels, `elapsed` ms after the change, for a card `h` pixels tall
inline int rollOffset(uint32_t elapsed, int h) { return (int)lroundf(easeOutCubic((float)elapsed / (float)ROLL_MS) * (float)h); }

// "09:41" -> the four digit characters; false (and "----") when the time is not known
inline bool timeDigits(bool valid, int hour, int min, char out[4]) {
  if (!valid || hour < 0 || hour > 23 || min < 0 || min > 59) { out[0] = out[1] = out[2] = out[3] = '-'; return false; }
  out[0] = (char)('0' + hour / 10); out[1] = (char)('0' + hour % 10);
  out[2] = (char)('0' + min / 10);  out[3] = (char)('0' + min % 10);
  return true;
}

}   // namespace facegeom
