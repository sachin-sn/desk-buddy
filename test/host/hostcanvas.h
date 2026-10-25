#pragma once
// A tiny software canvas with the same drawing calls the eyes renderer uses on the
// board (LovyanGFX sprite): fillScreen, fillRoundRect, fillTriangle. RGB565 pixels.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

struct HostCanvas {
  static const int W = 240, H = 240;
  std::vector<uint16_t> px;

  HostCanvas() : px(W * H, 0) {}

  void set(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = c; }
  uint16_t get(int x, int y) const { return px[y * W + x]; }

  void fillScreen(uint16_t c) { for (auto& p : px) p = c; }

  void fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) set(i, j, c);
  }

  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        int dx = 0, dy = 0;                       // distance into a corner zone
        if (i < r) dx = r - i - 1; else if (i >= w - r) dx = i - (w - r);
        if (j < r) dy = r - j - 1; else if (j >= h - r) dy = j - (h - r);
        bool inCornerZone = (i < r || i >= w - r) && (j < r || j >= h - r);
        if (inCornerZone && dx * dx + dy * dy > r * r - r) continue;   // outside the arc
        set(x + i, y + j, c);
      }
    }
  }

  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std_min3(x0, x1, x2), maxx = std_max3(x0, x1, x2);
    int miny = std_min3(y0, y1, y2), maxy = std_max3(y0, y1, y2);
    for (int y = miny; y <= maxy; y++) {
      for (int x = minx; x <= maxx; x++) {
        long d1 = edge(x, y, x0, y0, x1, y1), d2 = edge(x, y, x1, y1, x2, y2), d3 = edge(x, y, x2, y2, x0, y0);
        bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(neg && pos)) set(x, y, c);
      }
    }
  }

  // number of pixels that are not `bg`
  int countNot(uint16_t bg) const { int n = 0; for (auto p : px) if (p != bg) n++; return n; }

  // bounding box of non-bg pixels; returns false if empty
  bool bounds(uint16_t bg, int& x0, int& y0, int& x1, int& y1) const {
    x0 = W; y0 = H; x1 = -1; y1 = -1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (px[y * W + x] != bg) {
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
    return x1 >= 0;
  }

 private:
  static int std_min3(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
  static int std_max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
  static long edge(int px_, int py_, int ax, int ay, int bx, int by) {
    return (long)(px_ - bx) * (ay - by) - (long)(ax - bx) * (py_ - by);
  }
};
