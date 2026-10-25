#pragma once
// The buddy's eyes: animation state machine + a renderer that only needs a few
// drawing primitives, so it runs on the board (LovyanGFX sprite) and on the host
// (test/host renders PNGs from the very same code).
//
// Required of the canvas type G (all with uint16_t RGB565 colours):
//   fillScreen(c)  fillRoundRect(x,y,w,h,r,c)  fillTriangle(x0,y0,x1,y1,x2,y2,c)
#include <stdint.h>
#include <math.h>

namespace eyes {

enum Mood : uint8_t { NORMAL = 0, HAPPY, TIRED, ANGRY, SURPRISED, SLEEP, MOOD_COUNT };

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline uint16_t rgb565(int r, int g, int b) {
  r = clamp255(r); g = clamp255(g); b = clamp255(b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Rgb { float r, g, b; };

inline Rgb moodColor(Mood m) {
  switch (m) {
    case HAPPY:     return {124, 255, 178};
    case TIRED:     return {130, 150, 185};
    case ANGRY:     return {255, 96, 64};
    case SURPRISED: return {255, 255, 255};
    case SLEEP:     return {70, 92, 130};
    default:        return {0, 209, 255};
  }
}

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 2463534242u) : s(seed ? seed : 1u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (next() >> 8) * (1.0f / 16777216.0f); }          // [0,1)
  float range(float a, float b) { return a + (b - a) * f(); }
};

class Engine {
 public:
  // nominal geometry in pixels on the 240x240 screen
  float eyeW = 70, eyeH = 88, radius = 28, gap = 22;
  float maxX = 16, maxY = 10;                                       // how far the gaze can travel

  explicit Engine(uint32_t seed = 1) : rng_(seed) { col_ = moodColor(NORMAL); }

  void setMood(Mood m) { mood_ = m; }
  Mood mood() const { return mood_; }

  // Look somewhere for a moment: x,y in -1..1 (right/down positive).
  void lookAt(float x, float y, uint32_t now, uint32_t holdMs = 900) {
    tx_ = clamp1(x); ty_ = clamp1(y);
    holdUntil_ = now + holdMs;
  }
  void blink(uint32_t now) { blinking_ = true; blinkStart_ = now; }
  void poke(uint32_t now) { pokeUntil_ = now + 700; }

  void update(uint32_t now) {
    if (!started_) { started_ = true; last_ = now; nextBlink_ = now + 1500; nextLook_ = now + 1200; }
    float dt = (float)(uint32_t)(now - last_);
    if (dt < 1.f) dt = 1.f;
    if (dt > 100.f) dt = 100.f;
    last_ = now;

    const bool asleep = mood_ == SLEEP;
    const bool poking = now < pokeUntil_;

    // blinking (with an occasional quick double blink)
    if (!asleep && !poking && !blinking_ && now >= nextBlink_) {
      blinking_ = true; blinkStart_ = now;
      nextBlink_ = now + (uint32_t)rng_.range(2200.f, 6500.f);
      if (rng_.f() < 0.15f) nextBlink_ = now + 330;
    }
    float open = 1.f;
    if (blinking_) {
      float t = (float)(uint32_t)(now - blinkStart_) / blinkMs();
      if (t >= 1.f) blinking_ = false;
      else open = 1.f - (t < 0.5f ? t * 2.f : (1.f - t) * 2.f);   // 1 -> 0 -> 1
    }
    if (asleep) open = 0.07f + 0.03f * sinf((float)now * 0.0017f);  // slow breathing
    open_ = open;

    // gaze: wander when nobody is steering it
    if (asleep) { tx_ = 0; ty_ = 0.4f; }
    else if (now >= holdUntil_ && now >= nextLook_) {
      if (rng_.f() < 0.35f) { tx_ = 0; ty_ = 0; }
      else { tx_ = rng_.range(-1.f, 1.f); ty_ = rng_.range(-0.7f, 0.7f); }
      nextLook_ = now + (uint32_t)rng_.range(1200.f, 4200.f);
    }
    const float ag = 1.f - expf(-dt / 120.f);
    ox_ += (tx_ * maxX - ox_) * ag;
    oy_ += (ty_ * maxY - oy_) * ag;

    // mood morphing: lids, size and colour ease toward the target
    const float am = 1.f - expf(-dt / 150.f);
    lidTired_ += ((mood_ == TIRED ? 1.f : 0.f) - lidTired_) * am;
    lidAngry_ += ((mood_ == ANGRY ? 1.f : 0.f) - lidAngry_) * am;
    lidHappy_ += ((mood_ == HAPPY ? 1.f : 0.f) - lidHappy_) * am;
    const float st = (poking || mood_ == SURPRISED) ? 1.10f : 1.f;
    scale_ += (st - scale_) * am;
    const Rgb c = moodColor(mood_);
    col_.r += (c.r - col_.r) * am; col_.g += (c.g - col_.g) * am; col_.b += (c.b - col_.b) * am;
  }

  float ox() const { return ox_; }
  float oy() const { return oy_; }
  float openAmt() const { return open_; }
  float scale() const { return scale_; }
  float lidTired() const { return lidTired_; }
  float lidAngry() const { return lidAngry_; }
  float lidHappy() const { return lidHappy_; }
  uint16_t color565() const { return rgb565((int)(col_.r + 0.5f), (int)(col_.g + 0.5f), (int)(col_.b + 0.5f)); }

 private:
  static float clamp1(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
  static float blinkMs() { return 170.f; }   // (function, not a constexpr member: the board builds as C++11)

  Rng rng_;
  Mood mood_ = NORMAL;
  bool started_ = false, blinking_ = false;
  uint32_t last_ = 0, nextBlink_ = 0, nextLook_ = 0, blinkStart_ = 0, holdUntil_ = 0, pokeUntil_ = 0;
  float tx_ = 0, ty_ = 0, ox_ = 0, oy_ = 0, open_ = 1;
  float lidTired_ = 0, lidAngry_ = 0, lidHappy_ = 0, scale_ = 1;
  Rgb col_{0, 209, 255};
};

// Draws one frame of eyes. The banner area (bottom) is left empty for the caller's text.
template <class G>
void draw(G& g, const Engine& e, uint16_t bg) {
  const float C = 120.f;
  g.fillScreen(bg);

  const float sc = e.scale();
  const float w = e.eyeW * sc;
  const float hFull = e.eyeH * sc;
  float h = hFull * e.openAmt();
  if (h < 5.f) h = 5.f;
  float rad = e.radius * sc;
  if (rad > w * 0.5f) rad = w * 0.5f;
  if (rad > h * 0.5f) rad = h * 0.5f;

  const float cx = C + e.ox();
  const float cy = C + e.oy() - 8.f;               // a little above centre: room for the banner
  const int lx = (int)lroundf(cx - e.gap * 0.5f - w);
  const int rx = (int)lroundf(cx + e.gap * 0.5f);
  const int y = (int)lroundf(cy - h * 0.5f);
  const int iw = (int)lroundf(w), ih = (int)lroundf(h), ir = (int)lroundf(rad);
  const uint16_t fg = e.color565();

  g.fillRoundRect(lx, y, iw, ih, ir, fg);
  g.fillRoundRect(rx, y, iw, ih, ir, fg);

  const float lidMax = h * 0.55f;
  const int tired = (int)lroundf(lidMax * e.lidTired());
  const int angry = (int)lroundf(lidMax * e.lidAngry());
  const int happy = (int)lroundf(h * 0.42f * e.lidHappy());
  if (tired > 0) {                                  // outer corners droop
    g.fillTriangle(lx, y - 1, lx + iw, y - 1, lx, y + tired, bg);
    g.fillTriangle(rx, y - 1, rx + iw, y - 1, rx + iw, y + tired, bg);
  }
  if (angry > 0) {                                  // inner corners dive
    g.fillTriangle(lx, y - 1, lx + iw, y - 1, lx + iw, y + angry, bg);
    g.fillTriangle(rx, y - 1, rx + iw, y - 1, rx, y + angry, bg);
  }
  if (happy > 0) {                                  // cheeks push the lower lid up
    g.fillRoundRect(lx - 1, y + ih - happy + 1, iw + 2, ih, ir, bg);
    g.fillRoundRect(rx - 1, y + ih - happy + 1, iw + 2, ih, ir, bg);
  }
}

// ---- which mood fits right now ----
struct MoodInput {
  int  hour = -1;          // local hour 0..23, -1 = time not known yet
  int  sleepFrom = 23, sleepTo = 7;
  int  minsToMeeting = -1; // minutes until the next meeting starts, -1 = none soon
  int  cpu = -1;           // Mac CPU %, -1 = unknown
  bool justTouched = false;
  unsigned idleMinutes = 0;
};

inline bool inSleepWindow(int hour, int from, int to) {
  if (hour < 0 || from == to) return false;
  return from < to ? (hour >= from && hour < to) : (hour >= from || hour < to);
}

inline Mood chooseMood(const MoodInput& in) {
  if (in.minsToMeeting >= 0 && in.minsToMeeting <= 10) return SURPRISED;   // heads up
  if (inSleepWindow(in.hour, in.sleepFrom, in.sleepTo)) return SLEEP;
  if (in.cpu >= 85) return ANGRY;                                          // Mac working hard
  if (in.justTouched) return HAPPY;
  if (in.hour >= 0 && in.idleMinutes >= 45 && (in.hour >= 21 || in.hour < 7)) return TIRED;
  return NORMAL;
}

}  // namespace eyes
