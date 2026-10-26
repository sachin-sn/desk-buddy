#pragma once
// The photo for the Photo clock face. The web editor crops it and shrinks it to 240x240 in the browser,
// then sends it as raw RGB565 (the screen's own pixel format), so the buddy needs no image decoder and
// never sees an oversized or odd file. Pure (no Arduino) so the file format is unit-tested on a PC.
//
// File layout (little-endian), 115 208 bytes:
//   bytes 0..3  "BPH1"        magic + version
//   bytes 4..5  width  (240)
//   bytes 6..7  height (240)
//   then 240*240 pixels, row by row, each pixel a 16-bit value: rrrrrggggggbbbbb
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PHOTO_W          240
#define PHOTO_H          240
#define PHOTO_HDR        8
#define PHOTO_PIXELS     (PHOTO_W * PHOTO_H)
#define PHOTO_PIXEL_BYTES (PHOTO_PIXELS * 2)
#define PHOTO_FILE_BYTES (PHOTO_HDR + PHOTO_PIXEL_BYTES)

inline void photoWriteHeader(uint8_t* b) {
  b[0] = 'B'; b[1] = 'P'; b[2] = 'H'; b[3] = '1';
  b[4] = PHOTO_W & 255; b[5] = PHOTO_W >> 8;
  b[6] = PHOTO_H & 255; b[7] = PHOTO_H >> 8;
}

// `n` is the number of bytes that were received (or read from the file).
inline bool photoHeaderOk(const uint8_t* b, size_t n) {
  if (!b || n != PHOTO_FILE_BYTES) return false;
  if (memcmp(b, "BPH1", 4) != 0) return false;
  int w = b[4] | (b[5] << 8), h = b[6] | (b[7] << 8);
  return w == PHOTO_W && h == PHOTO_H;
}

inline uint16_t photoRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
