// Clock faces: ids and names, accent colour rules, hand geometry, gliding seconds, tacho needle and rev
// sweep, roll animation, and the photo file format.
#include <set>
#include <string>
#include "check.h"
#include "facegeom.h"
#include "faceids.h"
#include "photo.h"

using namespace facegeom;

int main() {
  // ---- ids and names
  CHECK_EQ(FACE_COUNT, 6);
  std::set<std::string> keys, titles;
  for (int i = 0; i < FACE_COUNT; i++) {
    CHECK(strlen(faceKey(i)) > 2);
    CHECK(strlen(faceTitle(i)) > 2);
    CHECK(strlen(faceBlurb(i)) > 20);
    keys.insert(faceKey(i));
    titles.insert(faceTitle(i));
  }
  CHECK_EQ(keys.size(), (size_t)FACE_COUNT);       // every face has its own thumbnail name and title
  CHECK_EQ(titles.size(), (size_t)FACE_COUNT);
  CHECK(faceIdOk(0));
  CHECK(faceIdOk(FACE_COUNT - 1));
  CHECK(!faceIdOk(FACE_COUNT));
  CHECK(!faceIdOk(-1));
  CHECK_EQ(faceClamp(3), 3);
  CHECK_EQ(faceClamp(99), FACE_CLASSIC);           // an unknown saved value falls back to the classic face
  CHECK_EQ(faceClamp(-4), FACE_CLASSIC);

  // ---- accent colour
  uint32_t c = 0;
  CHECK(colorParse("#00D1FF", c)); CHECK_EQ(c, 0x00D1FF);
  CHECK(colorParse("#ff8a65", c)); CHECK_EQ(c, 0xFF8A65);
  CHECK(!colorParse("00D1FF", c));                 // needs the #
  CHECK(!colorParse("#00D1F", c));                 // too short
  CHECK(!colorParse("#00D1FFF", c));               // too long
  CHECK(!colorParse("#00D1GG", c));                // not hex
  CHECK(!colorParse("", c));
  CHECK(!colorParse(nullptr, c));
  char buf[8];
  colorFormat(0x0A0B0C, buf); CHECK_STR(buf, "#0A0B0C");
  colorFormat(ACCENT_DEFAULT, buf); CHECK_STR(buf, "#00D1FF");
  CHECK(colorParse(buf, c) && c == ACCENT_DEFAULT);
  CHECK(accentVisible(0x00D1FF));
  CHECK(accentVisible(0xFF0000));
  CHECK(accentVisible(0x606060));
  CHECK(!accentVisible(0x000000));                 // black on black
  CHECK(!accentVisible(0x202020));
  CHECK(!accentVisible(0x101050));

  CHECK_EQ(photoDimClamp(-5), 0);
  CHECK_EQ(photoDimClamp(40), 40);
  CHECK_EQ(photoDimClamp(500), PHOTO_DIM_MAX);

  // ---- hands
  CHECK_EQ((int)secondAngle(0), 0);
  CHECK_EQ((int)secondAngle(15), 90);
  CHECK_EQ((int)secondAngle(30.5f), 183);
  CHECK_EQ((int)minuteAngle(0, 0), 0);
  CHECK_EQ((int)minuteAngle(30, 0), 180);
  CHECK_EQ((int)minuteAngle(15, 30), 93);          // 15 min 30 s = 93 degrees
  CHECK_EQ((int)hourAngle(3, 0, 0), 90);
  CHECK_EQ((int)hourAngle(15, 0, 0), 90);          // 3 pm looks like 3 am on a 12-hour dial
  CHECK_EQ((int)hourAngle(12, 0, 0), 0);
  CHECK_EQ((int)hourAngle(0, 30, 0), 15);          // half past 12: the hour hand is half way to 1
  CHECK_EQ((int)hourAngle(10, 10, 0), 305);        // the famous 10:10 pose
  CHECK_EQ((int)minuteAngle(10, 0), 60);
  CHECK_EQ((int)wrap360(-90), 270);
  CHECK_EQ((int)wrap360(725), 5);

  Pt p = polar(120, 120, 100, 0);   CHECK(p.x == 120 && p.y == 20);      // 12 o'clock is straight up
  p = polar(120, 120, 100, 90);     CHECK(p.x == 220 && p.y == 120);     // 3 o'clock
  p = polar(120, 120, 100, 180);    CHECK(p.x == 120 && p.y == 220);
  p = polar(120, 120, 100, 270);    CHECK(p.x == 20 && p.y == 120);
  p = polar(120, 120, 100, 45);     CHECK(abs(p.x - 191) <= 1 && abs(p.y - 49) <= 1);
  p = polar(120, 120, 0, 123);      CHECK(p.x == 120 && p.y == 120);

  // every hand tip stays inside the round screen, whatever the time
  for (int h = 0; h < 24; h++)
    for (int m = 0; m < 60; m += 7)
      for (int s = 0; s < 60; s += 11) {
        Pt a = polar(120, 120, 112, hourAngle(h, m, (float)s));
        Pt b = polar(120, 120, 112, minuteAngle(m, (float)s));
        Pt d = polar(120, 120, 112, secondAngle((float)s));
        for (Pt q : {a, b, d}) CHECK((q.x - 120) * (q.x - 120) + (q.y - 120) * (q.y - 120) <= 113 * 113);
      }

  // ---- gliding seconds
  SecondTracker st;
  CHECK(st.frac(10, 5000) == 0.f);                 // first sight of a second: starts at 0
  CHECK(fabsf(st.frac(10, 5250) - 0.25f) < 0.001f);
  CHECK(fabsf(st.frac(10, 5900) - 0.9f) < 0.001f);
  CHECK(st.frac(10, 6400) <= 0.999f);              // a late tick never reaches the next second
  CHECK(st.frac(11, 6410) == 0.f);                 // the second changed: start again
  CHECK(fabsf(st.frac(11, 6660) - 0.25f) < 0.001f);
  CHECK(st.frac(0, 100000) == 0.f);                // wrap from 59 to 0 or a clock resync
  uint32_t before = 0xFFFFFF00u;                   // millis() wraps after 49 days: still fine
  SecondTracker w;
  w.frac(5, before);
  CHECK(fabsf(w.frac(5, before + 300) - 0.3f) < 0.001f);

  // ---- tacho
  CHECK_EQ((int)tachoAngle(0.f), 225);
  CHECK_EQ((int)tachoAngle(0.5f), 0);              // half way is straight up
  CHECK_EQ((int)tachoAngle(1.f), 135);
  CHECK_EQ((int)tachoAngle(-3.f), 225);            // clamped
  CHECK_EQ((int)tachoAngle(9.f), 135);
  CHECK_EQ((int)tachoAngle(10.f / 60.f), 270);     // 10 s on the dial
  CHECK(revSweep(0) == 0.f);
  float prev = 0.f;
  for (uint32_t t = 0; t <= REV_UP_MS; t += 10) {  // rises, never falls, while revving up
    float v = revSweep(t);
    CHECK(v >= prev - 0.0001f && v >= 0.f && v <= 1.f);
    prev = v;
  }
  CHECK(revSweep(REV_UP_MS) > 0.999f);
  CHECK(revSweep(REV_UP_MS + REV_HOLD_MS / 2) == 1.f);
  prev = 1.f;
  for (uint32_t t = REV_UP_MS + REV_HOLD_MS; t < REV_TOTAL_MS; t += 10) {   // and only falls afterwards
    float v = revSweep(t);
    CHECK(v <= prev + 0.0001f && v >= 0.f);
    prev = v;
  }
  CHECK(revSweep(REV_TOTAL_MS - 1) < 0.01f);
  CHECK(revSweep(REV_TOTAL_MS) < 0.f);             // over
  CHECK(revSweep(999999) < 0.f);

  // ---- roll
  CHECK_EQ(rollOffset(0, 60), 0);
  CHECK_EQ(rollOffset(ROLL_MS, 60), 60);
  CHECK_EQ(rollOffset(ROLL_MS * 4, 60), 60);       // stays put once finished
  CHECK(rollOffset(ROLL_MS / 2, 60) > 30);         // ease-out: more than half way at half time
  int last = 0;
  for (uint32_t t = 0; t <= ROLL_MS; t += 8) { int o = rollOffset(t, 64); CHECK(o >= last); last = o; }
  char d[4];
  CHECK(timeDigits(true, 9, 5, d)); CHECK(d[0] == '0' && d[1] == '9' && d[2] == '0' && d[3] == '5');
  CHECK(timeDigits(true, 23, 59, d)); CHECK(d[0] == '2' && d[1] == '3' && d[2] == '5' && d[3] == '9');
  CHECK(timeDigits(true, 0, 0, d)); CHECK(d[0] == '0' && d[3] == '0');
  CHECK(!timeDigits(false, 9, 5, d)); CHECK(d[0] == '-' && d[3] == '-');
  CHECK(!timeDigits(true, 24, 0, d));

  // ---- photo file
  static uint8_t f[PHOTO_FILE_BYTES];
  memset(f, 0, sizeof f);
  CHECK_EQ(PHOTO_FILE_BYTES, 115208);
  CHECK(!photoHeaderOk(f, PHOTO_FILE_BYTES));      // no magic yet
  photoWriteHeader(f);
  CHECK(photoHeaderOk(f, PHOTO_FILE_BYTES));
  CHECK(!photoHeaderOk(f, PHOTO_FILE_BYTES - 1));  // truncated upload
  CHECK(!photoHeaderOk(f, PHOTO_FILE_BYTES + 2));  // too much data
  CHECK(!photoHeaderOk(nullptr, PHOTO_FILE_BYTES));
  CHECK(!photoHeaderOk(f, 0));
  uint8_t keep = f[4];
  f[4] = 100; CHECK(!photoHeaderOk(f, PHOTO_FILE_BYTES)); f[4] = keep;   // wrong width
  f[3] = '2';  CHECK(!photoHeaderOk(f, PHOTO_FILE_BYTES)); f[3] = '1';   // other version
  CHECK(photoHeaderOk(f, PHOTO_FILE_BYTES));
  CHECK_EQ(photoRgb565(255, 255, 255), 0xFFFF);
  CHECK_EQ(photoRgb565(0, 0, 0), 0);
  CHECK_EQ(photoRgb565(255, 0, 0), 0xF800);
  CHECK_EQ(photoRgb565(0, 255, 0), 0x07E0);
  CHECK_EQ(photoRgb565(0, 0, 255), 0x001F);
  return finish();
}
