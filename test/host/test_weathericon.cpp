// Weather icons: code -> icon mapping, and every icon stays inside its box and draws something.
#include <set>
#include <string>
#include <vector>
#include "check.h"
#include "weathericon.h"

// Records what an icon draws: a bounding box (round ends included) and a text signature.
struct Recorder {
  int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999, n = 0;
  std::string sig;
  void grow(int ax, int ay, int bx, int by) {
    if (ax < x0) x0 = ax; if (ay < y0) y0 = ay; if (bx > x1) x1 = bx; if (by > y1) y1 = by;
  }
  void circle(int cx, int cy, int r, uint32_t c) {
    grow(cx - r, cy - r, cx + r, cy + r); n++;
    sig += "c" + std::to_string(cx) + "," + std::to_string(cy) + "," + std::to_string(r) + "," + std::to_string(c) + ";";
  }
  void line(int ax, int ay, int bx, int by, int w, uint32_t c) {
    int h = (w + 1) / 2;
    grow((ax < bx ? ax : bx) - h, (ay < by ? ay : by) - h, (ax > bx ? ax : bx) + h, (ay > by ? ay : by) + h); n++;
    sig += "l" + std::to_string(ax) + "," + std::to_string(ay) + "," + std::to_string(bx) + "," + std::to_string(by) + "," + std::to_string(w) + "," + std::to_string(c) + ";";
  }
  void rrect(int ax, int ay, int bx, int by, int r, uint32_t c) {
    grow(ax, ay, bx, by); n++;
    sig += "r" + std::to_string(ax) + "," + std::to_string(ay) + "," + std::to_string(bx) + "," + std::to_string(by) + "," + std::to_string(r) + "," + std::to_string(c) + ";";
  }
};

int main() {
  // every WMO code Open-Meteo can return maps to the icon a person would expect
  CHECK_EQ(weatherIcon(0, true), WI_SUN);
  CHECK_EQ(weatherIcon(0, false), WI_MOON);
  CHECK_EQ(weatherIcon(1, true), WI_SUN);
  CHECK_EQ(weatherIcon(1, false), WI_MOON);
  CHECK_EQ(weatherIcon(2, true), WI_PARTLY_DAY);
  CHECK_EQ(weatherIcon(2, false), WI_PARTLY_NIGHT);
  CHECK_EQ(weatherIcon(3, true), WI_CLOUD);
  CHECK_EQ(weatherIcon(3, false), WI_CLOUD);
  CHECK_EQ(weatherIcon(45, true), WI_FOG);
  CHECK_EQ(weatherIcon(48, false), WI_FOG);
  const int drizzle[] = {51, 53, 55, 56, 57};
  for (int c : drizzle) CHECK_EQ(weatherIcon((uint8_t)c, true), WI_DRIZZLE);
  const int rain[] = {61, 63, 65, 66, 67, 80, 81, 82};
  for (int c : rain) { CHECK_EQ(weatherIcon((uint8_t)c, true), WI_RAIN); CHECK_EQ(weatherIcon((uint8_t)c, false), WI_RAIN); }
  const int snow[] = {71, 73, 75, 77, 85, 86};
  for (int c : snow) CHECK_EQ(weatherIcon((uint8_t)c, true), WI_SNOW);
  const int storm[] = {95, 96, 99};
  for (int c : storm) CHECK_EQ(weatherIcon((uint8_t)c, false), WI_STORM);
  CHECK_EQ(weatherIcon(200, true), WI_CLOUD);      // unknown code: a neutral icon, not a crash
  CHECK_EQ(weatherIcon(255, false), WI_CLOUD);

  // each icon draws something, fits inside WXI_W x WXI_H, is not tiny, and differs from the others
  std::set<std::string> seen;
  for (int k = 0; k < WI_COUNT; k++) {
    Recorder r;
    weatherIconDraw(r, (WxIcon)k);
    CHECK(r.n >= 2);
    CHECK(r.x0 >= 0 && r.y0 >= 0);
    CHECK(r.x1 < WXI_W && r.y1 < WXI_H);
    CHECK(r.x1 - r.x0 >= 40);                      // at least 40 px wide, so it reads from a desk
    CHECK(r.y1 - r.y0 >= 40);
    if (r.x0 < 0 || r.y0 < 0 || r.x1 >= WXI_W || r.y1 >= WXI_H)
      printf("  icon %d box %d,%d - %d,%d\n", k, r.x0, r.y0, r.x1, r.y1);
    CHECK(seen.insert(r.sig).second);
  }
  CHECK_EQ(seen.size(), (size_t)WI_COUNT);

  // an out-of-range kind draws nothing
  Recorder none;
  weatherIconDraw(none, (WxIcon)WI_COUNT);
  CHECK_EQ(none.n, 0);
  return finish();
}
