// Stand-in for photostore.cpp on the PC: a synthetic 240x240 "photo" (sunset over hills), or a real
// photo.bin when the PHOTO_BIN environment variable names one (the file the web editor uploads).
#include <stdio.h>
#include <stdlib.h>
#include "photostore.h"

static uint8_t  file[PHOTO_FILE_BYTES];
static bool     have = true;
static uint32_t ver = 1;

static void makeSunset() {
  photoWriteHeader(file);
  uint16_t* px = (uint16_t*)(file + PHOTO_HDR);
  for (int y = 0; y < PHOTO_H; y++)
    for (int x = 0; x < PHOTO_W; x++) {
      float t = y / (float)PHOTO_H;
      int r = (int)(40 + 215 * t), g = (int)(30 + 120 * t * t), b = (int)(110 - 80 * t);
      int dx = x - 150, dy = y - 150;
      if (dx * dx + dy * dy < 34 * 34) { r = 255; g = 225; b = 140; }                 // sun
      int hill = 190 + (int)(18 * __builtin_sinf(x / 30.f)) + (x > 120 ? 10 : 0);
      if (y > hill) { r = 12; g = 22; b = 20; }                                       // dark hills
      if (r > 255) r = 255; if (g > 255) g = 255; if (b < 0) b = 0;
      px[y * PHOTO_W + x] = photoRgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

static void init() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* path = getenv("PHOTO_BIN");
  if (path) {
    FILE* f = fopen(path, "rb");
    if (f) { size_t n = fread(file, 1, sizeof file, f); fclose(f); if (photoHeaderOk(file, n)) return; }
  }
  makeSunset();
}

void photoStubSet(bool present) { have = present; ver++; }

PhotoRef photoCurrent() {
  init();
  PhotoRef r;
  r.px = have ? (const uint16_t*)(file + PHOTO_HDR) : nullptr;
  r.ver = ver;
  return r;
}
void photoSeen(uint32_t) {}
bool photoPresent() { return have; }
