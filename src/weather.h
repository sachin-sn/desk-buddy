#pragma once
// Weather data, the words and colours for it, and the request URL. Pure (no Arduino), so it is unit-tested on a PC.
// Data comes from Open-Meteo (https://open-meteo.com): free, no account or API key.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define WX_DEG "\xC2\xB0"     // the degree sign in UTF-8; LVGL's built-in fonts include U+00B0

struct Weather {
  bool     valid;
  bool     isDay;
  char     unit;              // 'C' or 'F'
  uint8_t  code;              // WMO weather code, see weatherWords()
  int16_t  temp, hi, lo;      // whole degrees in `unit`; hi/lo are today's extremes
  uint32_t fetched;           // unix seconds when it was fetched
};

inline int16_t weatherRound(float v) {
  if (!(v == v)) return 0;                                  // NaN
  if (v > 150.f) v = 150.f;
  if (v < -150.f) v = -150.f;
  return (int16_t)(v + (v >= 0 ? 0.5f : -0.5f));
}

// WMO weather interpretation codes -> short words (13 characters at most, they have to fit the round screen).
inline const char* weatherWords(uint8_t code, bool isDay) {
  switch (code) {
    case 0:  return isDay ? "Clear" : "Clear night";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Icy drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Hail storm";
    default: return "Weather";
  }
}

// 0xRRGGBB for the weather line on the clock.
inline uint32_t weatherColor(uint8_t code, bool isDay) {
  if (code == 0 || code == 1) return isDay ? 0xFFC857 : 0xB9C4E6;       // sun / clear night
  if (code == 2 || code == 3 || code == 45 || code == 48) return 0xB0B6BE;   // cloud, fog
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 0x6CB4FF;   // rain
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 0xE6F2FF;     // snow
  if (code >= 95) return 0xB388FF;                                                   // storm
  return 0x9AA0A6;
}

// Shown only while the reading is recent; after maxAgeSec without a successful fetch it disappears
// rather than showing an old temperature as if it were current.
inline bool weatherFresh(const Weather& w, uint32_t now, uint32_t maxAgeSec = 3UL * 3600UL) {
  if (!w.valid || now == 0) return false;
  int32_t age = (int32_t)(now - w.fetched);
  return age >= -60 && age <= (int32_t)maxAgeSec;
}

// "Bengaluru, Karnataka, India" -> "Bengaluru": the part before the first comma, for the weather screen.
inline void weatherCityName(const char* place, char* out, size_t n) {
  if (n == 0) return;
  size_t o = 0;
  const char* p = place ? place : "";
  while (*p == ' ') p++;
  for (; *p && *p != ',' && o + 1 < n; p++) out[o++] = *p;
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
}

// Latitude/longitude must be real numbers in range. (0, 0) is open ocean and means "not set".
inline bool weatherCoordsOk(float lat, float lon) {
  if (!(lat == lat) || !(lon == lon)) return false;
  if (lat < -90.f || lat > 90.f || lon < -180.f || lon > 180.f) return false;
  return !(lat == 0.f && lon == 0.f);
}

// Open-Meteo request: current conditions plus today's high and low. Returns the length (or >= n if it did not fit).
inline int weatherUrl(char* out, size_t n, float lat, float lon, char unit) {
  return snprintf(out, n,
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min"
    "&forecast_days=1&timezone=auto%s",
    (double)lat, (double)lon, unit == 'F' ? "&temperature_unit=fahrenheit" : "");
}
