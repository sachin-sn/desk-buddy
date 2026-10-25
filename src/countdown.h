#pragma once
// Countdown timer logic. No Arduino dependencies: time is passed in, so the same
// code runs on the board and in the host tests (test/host).
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

class Countdown {
 public:
  enum State : uint8_t { IDLE = 0, RUNNING = 1, PAUSED = 2, DONE = 3 };
  static const uint32_t MIN_S = 60;
  static const uint32_t MAX_S = 99u * 3600u + 59u * 60u;

  State    state() const { return st_; }
  uint32_t duration() const { return dur_; }

  // Duration can only change while nothing is counting.
  void setDuration(uint32_t s) {
    if (st_ == RUNNING || st_ == PAUSED) return;
    dur_ = clampDur(s);
    remMs_ = (uint64_t)dur_ * 1000u;
    st_ = IDLE;
  }

  // Knob step: 1 min below 10 min, 5 min below 1 h, 15 min above. Steps land on multiples.
  static uint32_t stepped(uint32_t d, int dir) {
    if (dir > 0) {
      uint32_t s = d < 600 ? 60 : d < 3600 ? 300 : 900;
      return clampDur((d / s + 1) * s);
    }
    if (dir < 0) {
      uint32_t base = d > 0 ? d - 1 : 0;
      uint32_t s = base < 600 ? 60 : base < 3600 ? 300 : 900;
      return clampDur((base / s) * s);
    }
    return d;
  }
  void adjust(int dir) { setDuration(stepped(dur_, dir)); }

  void start(uint64_t nowMs) {
    if (st_ == RUNNING) return;
    if (st_ == IDLE || st_ == DONE) remMs_ = (uint64_t)dur_ * 1000u;
    endMs_ = nowMs + remMs_;
    st_ = RUNNING;
  }
  void pause(uint64_t nowMs) {
    if (st_ != RUNNING) return;
    remMs_ = endMs_ > nowMs ? endMs_ - nowMs : 0;
    st_ = PAUSED;
  }
  void toggle(uint64_t nowMs) {
    if (st_ == RUNNING) pause(nowMs); else start(nowMs);
  }
  void reset() {
    st_ = IDLE;
    remMs_ = (uint64_t)dur_ * 1000u;
  }

  // Call regularly. Returns true exactly once, when the countdown reaches zero.
  bool tick(uint64_t nowMs) {
    if (st_ == RUNNING && nowMs >= endMs_) {
      st_ = DONE;
      remMs_ = 0;
      return true;
    }
    return false;
  }

  uint64_t remainingMs(uint64_t nowMs) const {
    switch (st_) {
      case RUNNING: return endMs_ > nowMs ? endMs_ - nowMs : 0;
      case DONE:    return 0;
      default:      return remMs_;
    }
  }
  uint32_t remainingSec(uint64_t nowMs) const { return (uint32_t)((remainingMs(nowMs) + 999u) / 1000u); }

  // Fraction elapsed, 0..1 (0 while idle).
  float progress(uint64_t nowMs) const {
    if (st_ == IDLE) return 0.f;
    if (st_ == DONE) return 1.f;
    float total = (float)dur_ * 1000.f;
    float left = (float)remainingMs(nowMs);
    float p = 1.f - left / total;
    return p < 0.f ? 0.f : (p > 1.f ? 1.f : p);
  }

  // Persistence across reboots. `remSec` is the remaining time at the moment of restore
  // (the caller derives it from a saved end-time and the current wall clock).
  void restore(State s, uint32_t dur, uint32_t remSec, uint64_t nowMs) {
    dur_ = clampDur(dur);
    remMs_ = (uint64_t)remSec * 1000u;
    switch (s) {
      case RUNNING:
        if (remSec == 0) { st_ = DONE; remMs_ = 0; }
        else { endMs_ = nowMs + remMs_; st_ = RUNNING; }
        break;
      case PAUSED: st_ = remSec ? PAUSED : DONE; break;
      case DONE:   st_ = DONE; remMs_ = 0; break;
      default:     st_ = IDLE; remMs_ = (uint64_t)dur_ * 1000u; break;
    }
  }

  // "MM:SS", or "H:MM:SS" from one hour up.
  static void format(char* out, size_t n, uint32_t sec) {
    if (sec >= 3600) snprintf(out, n, "%u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
    else             snprintf(out, n, "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
  }

  static uint32_t clampDur(uint32_t s) { return s < MIN_S ? MIN_S : (s > MAX_S ? MAX_S : s); }

 private:
  State    st_ = IDLE;
  uint32_t dur_ = 25 * 60;
  uint64_t remMs_ = 25ull * 60 * 1000;
  uint64_t endMs_ = 0;
};
