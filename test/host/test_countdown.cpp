#include "check.h"
#include "countdown.h"

int main() {
  // knob stepping
  CHECK_EQ(Countdown::stepped(25 * 60, +1), 30 * 60);
  CHECK_EQ(Countdown::stepped(25 * 60, -1), 20 * 60);
  CHECK_EQ(Countdown::stepped(3 * 60, +1), 4 * 60);
  CHECK_EQ(Countdown::stepped(3 * 60, -1), 2 * 60);
  CHECK_EQ(Countdown::stepped(10 * 60, +1), 15 * 60);
  CHECK_EQ(Countdown::stepped(10 * 60, -1), 9 * 60);
  CHECK_EQ(Countdown::stepped(60, -1), 60);                      // floor at 1 minute
  CHECK_EQ(Countdown::stepped(3600, +1), 4500);
  CHECK_EQ(Countdown::stepped(3600, -1), 3300);
  CHECK_EQ(Countdown::stepped(Countdown::MAX_S, +1), (long long)Countdown::MAX_S);

  // idle -> running -> done
  Countdown c;
  c.setDuration(90);
  CHECK_EQ(c.state(), Countdown::IDLE);
  CHECK_EQ(c.remainingSec(0), 90);
  c.start(1000);
  CHECK_EQ(c.state(), Countdown::RUNNING);
  CHECK_EQ(c.remainingSec(1000), 90);
  CHECK_EQ(c.remainingSec(1001), 90);                            // rounds up while a fraction remains
  CHECK_EQ(c.remainingSec(1000 + 89000), 1);
  CHECK(!c.tick(1000 + 89999));
  CHECK(c.tick(1000 + 90000));                                   // fires exactly once
  CHECK(!c.tick(1000 + 90001));
  CHECK_EQ(c.state(), Countdown::DONE);
  CHECK_EQ(c.remainingSec(999999), 0);

  // duration is locked while running or paused
  c.reset();
  c.setDuration(120);
  c.start(0);
  c.setDuration(600);
  CHECK_EQ(c.duration(), 120);
  c.adjust(+1);
  CHECK_EQ(c.duration(), 120);

  // pause keeps the remaining time, resume continues from it
  c.pause(30000);
  CHECK_EQ(c.state(), Countdown::PAUSED);
  CHECK_EQ(c.remainingSec(30000), 90);
  CHECK_EQ(c.remainingSec(500000), 90);                          // paused: time does not move
  c.start(500000);
  CHECK_EQ(c.state(), Countdown::RUNNING);
  CHECK_EQ(c.remainingSec(500000 + 10000), 80);

  // progress
  Countdown p;
  p.setDuration(100);
  CHECK(p.progress(0) == 0.f);
  p.start(0);
  CHECK(p.progress(50000) > 0.49f && p.progress(50000) < 0.51f);
  p.tick(100000);
  CHECK(p.progress(100001) == 1.f);

  // toggle and restart after DONE
  Countdown t;
  t.setDuration(60);
  t.toggle(0);                                                   // start
  CHECK_EQ(t.state(), Countdown::RUNNING);
  t.toggle(10000);                                               // pause
  CHECK_EQ(t.state(), Countdown::PAUSED);
  t.start(10000);
  t.tick(80000);
  CHECK_EQ(t.state(), Countdown::DONE);
  t.start(90000);                                                // starting again after DONE restarts the full duration
  CHECK_EQ(t.state(), Countdown::RUNNING);
  CHECK_EQ(t.remainingSec(90000), 60);
  t.reset();
  CHECK_EQ(t.state(), Countdown::IDLE);
  CHECK_EQ(t.remainingSec(0), 60);

  // persistence
  Countdown r;
  r.restore(Countdown::RUNNING, 1500, 600, 5000);
  CHECK_EQ(r.state(), Countdown::RUNNING);
  CHECK_EQ(r.remainingSec(5000), 600);
  CHECK_EQ(r.duration(), 1500);
  r.restore(Countdown::RUNNING, 1500, 0, 5000);                  // ran out while the device was off
  CHECK_EQ(r.state(), Countdown::DONE);
  r.restore(Countdown::PAUSED, 1500, 300, 5000);
  CHECK_EQ(r.state(), Countdown::PAUSED);
  CHECK_EQ(r.remainingSec(99999), 300);
  r.restore(Countdown::IDLE, 600, 0, 0);
  CHECK_EQ(r.state(), Countdown::IDLE);
  CHECK_EQ(r.remainingSec(0), 600);

  // clamp
  Countdown k;
  k.setDuration(5);
  CHECK_EQ(k.duration(), Countdown::MIN_S);
  k.setDuration(10u * 1000u * 1000u);
  CHECK_EQ(k.duration(), (long long)Countdown::MAX_S);

  // formatting
  char b[16];
  Countdown::format(b, sizeof b, 0);        CHECK_STR(b, "00:00");
  Countdown::format(b, sizeof b, 65);       CHECK_STR(b, "01:05");
  Countdown::format(b, sizeof b, 1500);     CHECK_STR(b, "25:00");
  Countdown::format(b, sizeof b, 3599);     CHECK_STR(b, "59:59");
  Countdown::format(b, sizeof b, 3600);     CHECK_STR(b, "1:00:00");
  Countdown::format(b, sizeof b, 5025);     CHECK_STR(b, "1:23:45");

  return finish();
}
