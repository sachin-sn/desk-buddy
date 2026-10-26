// Renders every clock face with the real LVGL into PPM files (see Makefile).
#include <string>
#include "faceids.h"
#include "photostore.h"
#include "ui_internal.h"

void photoStubSet(bool present);

static uint32_t fakeMs = 100000;
extern "C" uint32_t millis() { return fakeMs; }

static lv_color_t fb[240 * 240];
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t drv;
static lv_color_t buf[240 * 40];

static void flush(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) fb[y * 240 + x] = *c++;
  lv_disp_flush_ready(d);
}
static void dump(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint32_t c = lv_color_to32(fb[i]);
    fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
  }
  fclose(f);
}

static Snapshot snapAt(int h, int m, int s, bool weather = true) {
  Snapshot sn;
  memset(&sn, 0, sizeof sn);
  sn.timeValid = true;
  sn.now = 1790000000;
  sn.t.tm_hour = h; sn.t.tm_min = m; sn.t.tm_sec = s;
  sn.t.tm_year = 126; sn.t.tm_mon = 9; sn.t.tm_mday = 26; sn.t.tm_wday = 1;
  if (weather) {
    sn.wx.valid = true; sn.wx.isDay = true; sn.wx.unit = 'C'; sn.wx.code = 2;
    sn.wx.temp = 27; sn.wx.hi = 31; sn.wx.lo = 22; sn.wx.fetched = sn.now - 120;
  }
  return sn;
}

struct Opt {
  uint32_t accent = ACCENT_DEFAULT;
  int dim = PHOTO_DIM_DEFAULT;
  const char* next = "Standup  in 12m";
  uint32_t nextColor = 0xFFB020;
};

static ClockView view(const Snapshot& sn, int face, const Opt& o) {
  ClockView v;
  v.s = &sn; v.nextLine = o.next; v.nextColor = o.nextColor; v.nowMs = fakeMs;
  v.face = (uint8_t)face; v.accent = o.accent; v.photoDim = (uint8_t)o.dim;
  return v;
}

static void settle() { for (int i = 0; i < 8; i++) lv_timer_handler(); }

// draws the face for the time `sn`, `glideMs` after the second started; optionally a second frame later
static void shot(const std::string& out, const char* name, int face, const Snapshot& sn, uint32_t glideMs = 0, Opt o = Opt()) {
  fakeMs += 5000;
  clockEnter(fakeMs);
  clockRefresh(view(sn, face, o));          // builds the face; seconds start now
  fakeMs += glideMs;
  clockRefresh(view(sn, face, o));
  lv_scr_load(screenFor(MODE_CLOCK));
  lv_obj_invalidate(lv_scr_act());
  settle();
  dump(out + "/lv_" + name + ".ppm");
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf, NULL, 240 * 40);
  lv_disp_drv_init(&drv);
  drv.hor_res = 240; drv.ver_res = 240; drv.flush_cb = flush; drv.draw_buf = &drawBuf;
  lv_disp_drv_register(&drv);
  screensBuild();

  Snapshot a = snapAt(10, 8, 37);
  shot(out, "classic", FACE_CLASSIC, a);
  shot(out, "analog", FACE_ANALOG, a, 450);
  shot(out, "minimal", FACE_MINIMAL, a, 450);
  shot(out, "tacho", FACE_TACHO, a, 450);            // 450 ms into the entry rev, so the needle is up
  shot(out, "roll", FACE_ROLL, a);
  shot(out, "photo", FACE_PHOTO, a);

  // other situations
  Snapshot b = snapAt(3, 15, 0, false);
  Opt none; none.next = "";
  shot(out, "classic_bare", FACE_CLASSIC, b, 0, none);
  shot(out, "analog_bare", FACE_ANALOG, b, 0, none);
  shot(out, "minimal_bare", FACE_MINIMAL, b, 0, none);
  shot(out, "tacho_bare", FACE_TACHO, b, 2000, none);  // after the rev: needle at the second
  shot(out, "roll_bare", FACE_ROLL, b, 0, none);
  Opt orange; orange.accent = 0xFF7A18;
  shot(out, "analog_orange", FACE_ANALOG, snapAt(10, 10, 30), 0, orange);
  shot(out, "tacho_orange", FACE_TACHO, snapAt(23, 59, 52), 2500, orange);
  shot(out, "minimal_orange", FACE_MINIMAL, snapAt(7, 45, 15), 0, orange);
  Snapshot nt = snapAt(0, 0, 0); nt.timeValid = false;
  shot(out, "classic_notime", FACE_CLASSIC, nt);
  shot(out, "analog_notime", FACE_ANALOG, nt);
  shot(out, "tacho_notime", FACE_TACHO, nt, 2000);
  shot(out, "roll_notime", FACE_ROLL, nt);
  shot(out, "photo_notime", FACE_PHOTO, nt);
  Opt dim0; dim0.dim = 0;
  shot(out, "photo_dim0", FACE_PHOTO, a, 0, dim0);
  Opt dim70; dim70.dim = 70;
  shot(out, "photo_dim70", FACE_PHOTO, a, 0, dim70);
  photoStubSet(false);
  shot(out, "photo_empty", FACE_PHOTO, a);
  photoStubSet(true);

  // the roll animation, caught mid-way: 10:07 -> 10:08
  {
    Snapshot t1 = snapAt(10, 7, 59), t2 = snapAt(10, 8, 0);
    Opt o;
    fakeMs += 5000; clockEnter(fakeMs);
    clockRefresh(view(t1, FACE_ROLL, o));
    fakeMs += 1000;
    clockRefresh(view(t1, FACE_ROLL, o));
    fakeMs += 33;
    clockRefresh(view(t2, FACE_ROLL, o));              // the minute changes now
    fakeMs += 45;
    clockRefresh(view(t2, FACE_ROLL, o));
    lv_scr_load(screenFor(MODE_CLOCK)); lv_obj_invalidate(lv_scr_act()); settle();
    dump(out + "/lv_roll_early.ppm");
    fakeMs += 75;
    clockRefresh(view(t2, FACE_ROLL, o));
    lv_scr_load(screenFor(MODE_CLOCK)); lv_obj_invalidate(lv_scr_act()); settle();
    dump(out + "/lv_roll_mid.ppm");
    fakeMs += 400;
    clockRefresh(view(t2, FACE_ROLL, o));
    settle();
    dump(out + "/lv_roll_done.ppm");
  }
  // face changes: each build and teardown, many times, to catch leaks and crashes
  for (int i = 0; i < 60; i++) { fakeMs += 100; clockRefresh(view(a, i % FACE_COUNT, Opt())); settle(); }
  printf("rendered\n");
  return 0;
}
