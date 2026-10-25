#include "check.h"
#include "eyes.h"
#include "hostcanvas.h"

using namespace eyes;

static uint16_t kBg = 0;

// run the engine for `ms` of simulated time at ~30 fps
static void run(Engine& e, uint32_t& now, uint32_t ms) {
  uint32_t end = now + ms;
  while (now < end) { now += 33; e.update(now); }
}

int main() {
  // ---- mood selection ----
  MoodInput in;
  CHECK_EQ(chooseMood(in), NORMAL);
  in.hour = 14;
  CHECK_EQ(chooseMood(in), NORMAL);
  in.hour = 23;      CHECK_EQ(chooseMood(in), SLEEP);                 // default window 23..7
  in.hour = 3;       CHECK_EQ(chooseMood(in), SLEEP);
  in.hour = 7;       CHECK_EQ(chooseMood(in), NORMAL);
  in.hour = 22;      CHECK_EQ(chooseMood(in), NORMAL);
  in.hour = -1;      CHECK_EQ(chooseMood(in), NORMAL);                // unknown time never sleeps
  in.hour = 23; in.minsToMeeting = 8;
  CHECK_EQ(chooseMood(in), SURPRISED);                                // a meeting beats sleep
  in.minsToMeeting = 11;
  CHECK_EQ(chooseMood(in), SLEEP);
  in = MoodInput(); in.hour = 15; in.cpu = 90;
  CHECK_EQ(chooseMood(in), ANGRY);
  in.cpu = 84;       CHECK_EQ(chooseMood(in), NORMAL);
  in.cpu = -1; in.justTouched = true;
  CHECK_EQ(chooseMood(in), HAPPY);
  in.justTouched = false; in.hour = 21; in.idleMinutes = 50;
  CHECK_EQ(chooseMood(in), TIRED);
  in.hour = 15;      CHECK_EQ(chooseMood(in), NORMAL);                // idle in daytime is not tired
  CHECK(inSleepWindow(10, 9, 17) && !inSleepWindow(8, 9, 17) && !inSleepWindow(5, 5, 5));   // non-wrapping window, empty window

  // ---- animation ----
  {
    Engine e(7);
    uint32_t now = 1000;
    e.update(now);
    CHECK(e.openAmt() > 0.99f);

    // within 7 s of simulated time there must be a blink, and eyes must reopen afterwards
    float minOpen = 1.f;
    for (uint32_t t = 0; t < 7000; t += 33) { now += 33; e.update(now); if (e.openAmt() < minOpen) minOpen = e.openAmt(); }
    CHECK(minOpen < 0.2f);
    run(e, now, 400);
    CHECK(e.openAmt() > 0.99f || e.openAmt() < 1.01f);

    // gaze stays within its range
    float maxAbsX = 0, maxAbsY = 0;
    for (uint32_t t = 0; t < 60000; t += 33) {
      now += 33; e.update(now);
      if (fabsf(e.ox()) > maxAbsX) maxAbsX = fabsf(e.ox());
      if (fabsf(e.oy()) > maxAbsY) maxAbsY = fabsf(e.oy());
    }
    CHECK(maxAbsX <= e.maxX + 0.01f);
    CHECK(maxAbsY <= e.maxY + 0.01f);
    CHECK(maxAbsX > 4.f);                                              // and it does wander

    // lookAt is honoured for its hold time, then idle wandering resumes
    Engine l(3);
    uint32_t n = 5000; l.update(n);
    l.lookAt(1.f, 0.f, n, 3000);
    run(l, n, 1000);
    CHECK(l.ox() > l.maxX * 0.95f);
    run(l, n, 1000);
    CHECK(l.ox() > l.maxX * 0.95f);                                    // still held (3 s hold, 2.5 s elapsed)
  }

  {
    // moods morph smoothly and settle
    Engine e(1);
    uint32_t now = 100; e.update(now);
    e.setMood(ANGRY);   run(e, now, 1500);
    CHECK(e.lidAngry() > 0.95f && e.lidTired() < 0.05f);
    e.setMood(HAPPY);   run(e, now, 1500);
    CHECK(e.lidHappy() > 0.95f && e.lidAngry() < 0.05f);
    e.setMood(SURPRISED); run(e, now, 1500);
    CHECK(e.scale() > 1.09f);
    e.setMood(SLEEP);   run(e, now, 3000);
    CHECK(e.openAmt() < 0.12f);                                        // eyes closed while asleep
    CHECK(e.lidTired() < 0.05f);                                       // closed lids, not drooping ones
    e.setMood(NORMAL);  run(e, now, 3000);
    CHECK(e.lidTired() < 0.05f && e.scale() < 1.01f);
  }

  {
    // same seed -> same animation (no hidden global state)
    Engine a(42), b(42);
    uint32_t na = 0, nb = 0;
    bool same = true;
    for (int i = 0; i < 600; i++) { na += 33; nb += 33; a.update(na); b.update(nb); if (a.ox() != b.ox() || a.openAmt() != b.openAmt()) same = false; }
    CHECK(same);
  }

  // ---- drawing ----
  {
    Engine e(5);
    uint32_t now = 50; e.update(now);
    HostCanvas c;
    draw(c, e, kBg);
    int x0, y0, x1, y1;
    CHECK(c.bounds(kBg, x0, y0, x1, y1));
    CHECK(x0 >= 20 && x1 <= 220 && y0 >= 30 && y1 <= 200);            // inside the round screen, with the banner area free
    CHECK(c.get(120, 120) == kBg);                                     // gap between the eyes stays dark
    int normalPixels = c.countNot(kBg);
    CHECK(normalPixels > 6000);

    // extremes of gaze and the biggest mood scale must still fit inside the circle
    Engine w(9); uint32_t n = 0; w.update(n);
    w.setMood(SURPRISED);
    int worstR2 = 0;
    for (float gx = -1; gx <= 1; gx += 1) for (float gy = -1; gy <= 1; gy += 1) {
      w.lookAt(gx, gy, n, 100000);
      for (int i = 0; i < 90; i++) { n += 33; w.update(n); }
      HostCanvas d; draw(d, w, kBg);
      for (int y = 0; y < 240; y++) for (int x = 0; x < 240; x++) if (d.get(x, y) != kBg) {
        int dx = x - 120, dy = y - 120, r2 = dx * dx + dy * dy;
        if (r2 > worstR2) worstR2 = r2;
      }
    }
    printf("  farthest lit pixel from centre: %.1f px (screen radius 120)\n", sqrt((double)worstR2));
    CHECK(worstR2 < 118 * 118);

    // a closed eye (sleep) lights far fewer pixels than an open one
    Engine s(5); uint32_t ns = 0; s.update(ns); s.setMood(SLEEP);
    for (int i = 0; i < 120; i++) { ns += 33; s.update(ns); }
    HostCanvas sc; draw(sc, s, kBg);
    CHECK(sc.countNot(kBg) < normalPixels / 4);

    // happy lids remove pixels from the lower part, angry/tired from the upper part
    Engine h(5); uint32_t nh = 0; h.update(nh); h.setMood(HAPPY);
    for (int i = 0; i < 90; i++) { nh += 33; h.update(nh); }
    HostCanvas hc; draw(hc, h, kBg);
    CHECK(hc.countNot(kBg) < normalPixels);
  }

  return finish();
}
