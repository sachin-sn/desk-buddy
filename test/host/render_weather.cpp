// Draws every weather icon with the same code the board uses (src/weathericon.h), through a small
// software painter, and writes one PPM per icon plus the numbers needed to build a contact sheet.
// Usage: render_weather <outdir>      then:  python3 -I make_weather_sheet.py <outdir>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "weathericon.h"

static const int SS = 4;                            // supersampling for smooth edges
struct SoftPainter {
  int W = WXI_W, H = WXI_H;
  std::vector<uint32_t> px;                         // SS x SS samples per output pixel
  SoftPainter() : px((size_t)W * SS * H * SS, 0) {}
  template <class F> void fill(float x0, float y0, float x1, float y1, uint32_t c, F inside) {
    int ix0 = (int)floorf(x0 * SS), iy0 = (int)floorf(y0 * SS), ix1 = (int)ceilf(x1 * SS), iy1 = (int)ceilf(y1 * SS);
    for (int y = iy0; y <= iy1; y++) for (int x = ix0; x <= ix1; x++) {
      if (x < 0 || y < 0 || x >= W * SS || y >= H * SS) continue;
      if (inside((x + 0.5f) / SS, (y + 0.5f) / SS)) px[(size_t)y * W * SS + x] = c | 0x1000000u;
    }
  }
  void circle(int cx, int cy, int r, uint32_t c) {
    float fx = cx + 0.5f, fy = cy + 0.5f, fr = r + 0.5f;
    fill(fx - fr, fy - fr, fx + fr, fy + fr, c, [=](float x, float y) { return (x - fx) * (x - fx) + (y - fy) * (y - fy) <= fr * fr; });
  }
  void line(int x1, int y1, int x2, int y2, int w, uint32_t c) {
    float ax = x1 + 0.5f, ay = y1 + 0.5f, bx = x2 + 0.5f, by = y2 + 0.5f, hw = w / 2.f;
    fill(fminf(ax, bx) - hw, fminf(ay, by) - hw, fmaxf(ax, bx) + hw, fmaxf(ay, by) + hw, c, [=](float x, float y) {
      float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
      float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
      t = fminf(1.f, fmaxf(0.f, t));
      float qx = ax + t * dx - x, qy = ay + t * dy - y;
      return qx * qx + qy * qy <= hw * hw;
    });
  }
  void rrect(int x1, int y1, int x2, int y2, int r, uint32_t c) {
    float L = (float)x1, T = (float)y1, R = (float)x2 + 1.f, B = (float)y2 + 1.f, rr = (float)r;
    if (rr > (R - L) / 2) rr = (R - L) / 2;
    if (rr > (B - T) / 2) rr = (B - T) / 2;
    fill(L, T, R, B, c, [=](float x, float y) {
      float cx = fminf(fmaxf(x, L + rr), R - rr), cy = fminf(fmaxf(y, T + rr), B - rr);
      return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr;
    });
  }
  // downsampled RGB, black where nothing was painted
  void write(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
      int r = 0, g = 0, b = 0;
      for (int j = 0; j < SS; j++) for (int i = 0; i < SS; i++) {
        uint32_t c = px[(size_t)(y * SS + j) * W * SS + x * SS + i];
        r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
      }
      int n = SS * SS;
      fputc(r / n, f); fputc(g / n, f); fputc(b / n, f);
    }
    fclose(f);
  }
};

static const char* NAMES[WI_COUNT] = {"sun", "moon", "partly_day", "partly_night", "cloud", "fog", "drizzle", "rain", "snow", "storm"};

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "out";
  for (int k = 0; k < WI_COUNT; k++) {
    SoftPainter p;
    weatherIconDraw(p, (WxIcon)k);
    p.write(out + "/wx_" + NAMES[k] + ".ppm");
  }
  printf("wrote %d icons to %s\n", (int)WI_COUNT, out.c_str());
  return 0;
}
