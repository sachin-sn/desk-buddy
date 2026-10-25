#pragma once
// Weather icons drawn from circles, thick lines and rounded rectangles, so there are no bitmaps to
// store. This file is pure (no Arduino, no LVGL): the drawing is written once against a small
// "painter" interface and used twice:
//   * on the board, screens.cpp passes a painter that calls LVGL's draw functions;
//   * on a PC, test/host/render_weather.cpp passes one that fills a picture, so the icons can be
//     looked at (and bounds-checked) without a display.
//
// A painter provides (all coordinates in pixels, relative to the icon's top-left corner):
//   void circle(int cx, int cy, int r, uint32_t rgb);                     // filled
//   void line(int x1, int y1, int x2, int y2, int width, uint32_t rgb);   // round ends
//   void rrect(int x1, int y1, int x2, int y2, int radius, uint32_t rgb); // filled, x2/y2 inclusive
// The icon is meant for a black background: the "bite" of the moon and the outline around a cloud that
// sits in front of the sun are painted in black.
#include <stdint.h>

#define WXI_W 104
#define WXI_H 80

enum WxIcon : uint8_t {
  WI_SUN, WI_MOON, WI_PARTLY_DAY, WI_PARTLY_NIGHT, WI_CLOUD, WI_FOG,
  WI_DRIZZLE, WI_RAIN, WI_SNOW, WI_STORM, WI_COUNT
};

// WMO weather interpretation code (see weather.h) -> icon.
inline WxIcon weatherIcon(uint8_t code, bool isDay) {
  switch (code) {
    case 0: case 1:   return isDay ? WI_SUN : WI_MOON;
    case 2:           return isDay ? WI_PARTLY_DAY : WI_PARTLY_NIGHT;
    case 3:           return WI_CLOUD;
    case 45: case 48: return WI_FOG;
    case 51: case 53: case 55: case 56: case 57: return WI_DRIZZLE;
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: return WI_RAIN;
    case 71: case 73: case 75: case 77: case 85: case 86: return WI_SNOW;
    case 95: case 96: case 99: return WI_STORM;
    default:          return WI_CLOUD;
  }
}

namespace wxi {

const uint32_t BLACK = 0x000000;
const uint32_t SUN = 0xFFC857, MOON = 0xDCE3F5;
const uint32_t CLOUD = 0xD3D9E1, STORM_CLOUD = 0x8E98A6;
const uint32_t RAIN = 0x6CB4FF, SNOW = 0xF2F8FF, BOLT = 0xFFD23F, FOG = 0xB0B6BE;

inline int sc(float v, float k) { return (int)(v * k + 0.5f); }

// A cloud whose bounding box is about 72k x 44k pixels with its top-left at (x, y).
// `grow` > 0 paints a fatter copy, used in black as an outline over whatever is behind the cloud.
template <class P> void cloud(P& p, int x, int y, float k, uint32_t col, int grow) {
  p.rrect(x + sc(2, k) - grow, y + sc(22, k) - grow, x + sc(70, k) + grow, y + sc(44, k) + grow, sc(11, k) + grow, col);
  p.circle(x + sc(22, k), y + sc(28, k), sc(12, k) + grow, col);
  p.circle(x + sc(40, k), y + sc(20, k), sc(19, k) + grow, col);
  p.circle(x + sc(57, k), y + sc(28, k), sc(13, k) + grow, col);
}

template <class P> void cloudWithOutline(P& p, int x, int y, float k, uint32_t col) {
  cloud(p, x, y, k, BLACK, 3);
  cloud(p, x, y, k, col, 0);
}

template <class P> void sun(P& p, int cx, int cy, int r, int w) {
  static const float dx[8] = {1.f, 0.7071f, 0.f, -0.7071f, -1.f, -0.7071f, 0.f, 0.7071f};
  static const float dy[8] = {0.f, 0.7071f, 1.f, 0.7071f, 0.f, -0.7071f, -1.f, -0.7071f};
  p.circle(cx, cy, r, SUN);
  int a = r + r * 4 / 9 + 1, b = r + r * 9 / 10 + 2;       // ray start and end distance from the centre
  for (int i = 0; i < 8; i++)
    p.line(cx + (int)(dx[i] * a + (dx[i] < 0 ? -0.5f : 0.5f)), cy + (int)(dy[i] * a + (dy[i] < 0 ? -0.5f : 0.5f)),
           cx + (int)(dx[i] * b + (dx[i] < 0 ? -0.5f : 0.5f)), cy + (int)(dy[i] * b + (dy[i] < 0 ? -0.5f : 0.5f)), w, SUN);
}

template <class P> void moon(P& p, int cx, int cy, int r) {
  p.circle(cx, cy, r, MOON);
  p.circle(cx + r * 9 / 20, cy - r * 7 / 20, r * 17 / 20, BLACK);     // the bite that makes the crescent
}

}   // namespace wxi

template <class P> void weatherIconDraw(P& p, WxIcon kind) {
  using namespace wxi;
  switch (kind) {
    case WI_SUN:
      sun(p, 52, 40, 16, 4);
      break;
    case WI_MOON:
      moon(p, 50, 40, 27);
      break;
    case WI_PARTLY_DAY:
      sun(p, 36, 30, 13, 3);
      cloudWithOutline(p, 28, 32, 1.0f, CLOUD);
      break;
    case WI_PARTLY_NIGHT:
      moon(p, 36, 30, 21);
      cloudWithOutline(p, 28, 32, 1.0f, CLOUD);
      break;
    case WI_CLOUD:
      cloud(p, 4, 10, 1.3f, CLOUD, 0);
      break;
    case WI_FOG:
      cloud(p, 16, 0, 1.0f, CLOUD, 0);
      p.line(20, 54, 82, 54, 5, FOG);
      p.line(30, 66, 90, 66, 5, FOG);
      break;
    case WI_DRIZZLE:
      cloud(p, 16, 0, 1.0f, STORM_CLOUD, 0);
      p.line(34, 54, 31, 61, 3, RAIN);
      p.line(52, 58, 49, 65, 3, RAIN);
      p.line(70, 54, 67, 61, 3, RAIN);
      break;
    case WI_RAIN:
      cloud(p, 16, 0, 1.0f, STORM_CLOUD, 0);
      p.line(32, 52, 27, 64, 4, RAIN);
      p.line(48, 56, 43, 68, 4, RAIN);
      p.line(64, 52, 59, 64, 4, RAIN);
      p.line(80, 56, 75, 68, 4, RAIN);
      break;
    case WI_SNOW:
      cloud(p, 16, 0, 1.0f, CLOUD, 0);
      p.circle(30, 56, 4, SNOW);
      p.circle(50, 60, 4, SNOW);
      p.circle(70, 56, 4, SNOW);
      p.circle(40, 71, 4, SNOW);
      p.circle(60, 71, 4, SNOW);
      break;
    case WI_STORM:
      cloud(p, 16, 0, 1.0f, STORM_CLOUD, 0);
      p.line(56, 38, 45, 56, 6, BOLT);
      p.line(45, 56, 60, 56, 6, BOLT);
      p.line(60, 56, 48, 74, 6, BOLT);
      break;
    default: break;
  }
}
