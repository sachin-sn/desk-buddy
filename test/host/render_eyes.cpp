// Renders the buddy's eyes with the exact code the firmware uses and writes PPM frames
// (240x240) for make_sheet.py to turn into a contact sheet.
#include <stdio.h>
#include <string>
#include "eyes.h"
#include "hostcanvas.h"

using namespace eyes;

static void save(const HostCanvas& c, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); return; }
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint16_t v = c.px[i];
    unsigned char rgb[3] = { (unsigned char)(((v >> 11) & 31) * 255 / 31), (unsigned char)(((v >> 5) & 63) * 255 / 63), (unsigned char)((v & 31) * 255 / 31) };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void settle(Engine& e, uint32_t& now, int frames) { for (int i = 0; i < frames; i++) { now += 33; e.update(now); } }

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  const char* names[] = {"normal", "happy", "tired", "angry", "surprised", "sleep"};
  for (int m = 0; m < MOOD_COUNT; m++) {
    Engine e(11);
    uint32_t now = 100; e.update(now);
    e.setMood((Mood)m);
    e.lookAt(0, 0, now, 100000);
    settle(e, now, 100);
    HostCanvas c; draw(c, e, 0);
    save(c, dir + "/mood_" + names[m] + ".ppm");
  }
  // normal eyes looking right/up, and mid-blink
  {
    Engine e(11); uint32_t now = 100; e.update(now);
    e.lookAt(1, -1, now, 100000); settle(e, now, 60);
    HostCanvas c; draw(c, e, 0); save(c, dir + "/look_right_up.ppm");
  }
  {
    Engine e(11); uint32_t now = 100; e.update(now);
    e.lookAt(0, 0, now, 100000); settle(e, now, 30);
    e.blink(now); now += 85; e.update(now);          // half-way through the blink
    HostCanvas c; draw(c, e, 0); save(c, dir + "/blink_mid.ppm");
  }
  printf("wrote frames to %s\n", dir.c_str());
  return 0;
}
