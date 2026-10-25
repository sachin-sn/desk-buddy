#include "check.h"
#include "weather.h"

int main() {
  // rounding to whole degrees (half away from zero), NaN and out-of-range safe
  CHECK_EQ(weatherRound(24.4f), 24);
  CHECK_EQ(weatherRound(24.5f), 25);
  CHECK_EQ(weatherRound(-0.4f), 0);
  CHECK_EQ(weatherRound(-0.6f), -1);
  CHECK_EQ(weatherRound(-3.5f), -4);
  CHECK_EQ(weatherRound(NAN), 0);
  CHECK_EQ(weatherRound(999.f), 150);
  CHECK_EQ(weatherRound(-999.f), -150);

  // every code has words that fit the round screen, with or without daylight
  for (int c = 0; c < 256; c++) for (int day = 0; day < 2; day++) {
    const char* w = weatherWords((uint8_t)c, day != 0);
    CHECK(w != nullptr && w[0] != 0);
    CHECK(strlen(w) <= 13);
  }
  CHECK_STR(weatherWords(0, true), "Clear");
  CHECK_STR(weatherWords(0, false), "Clear night");
  CHECK_STR(weatherWords(2, true), "Partly cloudy");
  CHECK_STR(weatherWords(63, true), "Rain");
  CHECK_STR(weatherWords(81, true), "Showers");
  CHECK_STR(weatherWords(95, true), "Thunderstorm");
  CHECK_STR(weatherWords(42, true), "Weather");            // unknown code, still shows something sensible

  // colours: sun, cloud, rain, snow, storm are all different
  CHECK(weatherColor(0, true) != weatherColor(0, false));
  CHECK(weatherColor(3, true) != weatherColor(63, true));
  CHECK(weatherColor(63, true) != weatherColor(73, true));
  CHECK(weatherColor(73, true) != weatherColor(95, true));
  CHECK_EQ(weatherColor(63, true), weatherColor(80, true));

  // freshness: valid, clock known, not older than 3 hours, tolerant of a small clock step back
  Weather w{}; w.valid = true; w.fetched = 1800000000;
  CHECK(weatherFresh(w, 1800000000));
  CHECK(weatherFresh(w, 1800000000 + 3 * 3600));
  CHECK(!weatherFresh(w, 1800000000 + 3 * 3600 + 1));
  CHECK(weatherFresh(w, 1800000000 - 30));
  CHECK(!weatherFresh(w, 1800000000 - 120));
  CHECK(!weatherFresh(w, 0));                                // clock not set yet
  w.valid = false;
  CHECK(!weatherFresh(w, 1800000000));

  // coordinates
  CHECK(weatherCoordsOk(12.9716f, 77.5946f));
  CHECK(weatherCoordsOk(-90.f, 180.f));
  CHECK(weatherCoordsOk(90.f, -180.f));
  CHECK(!weatherCoordsOk(0.f, 0.f));                         // "not set"
  CHECK(!weatherCoordsOk(90.1f, 10.f));
  CHECK(!weatherCoordsOk(10.f, 180.1f));
  CHECK(!weatherCoordsOk(NAN, 10.f));
  CHECK(!weatherCoordsOk(10.f, NAN));

  // request URL
  char url[260];
  int n = weatherUrl(url, sizeof url, 12.9716f, 77.5946f, 'C');
  CHECK(n > 0 && n < (int)sizeof url);
  CHECK(strstr(url, "https://api.open-meteo.com/v1/forecast?") == url);
  CHECK(strstr(url, "latitude=12.9716&longitude=77.5946") != nullptr);
  CHECK(strstr(url, "current=temperature_2m,weather_code,is_day") != nullptr);
  CHECK(strstr(url, "daily=temperature_2m_max,temperature_2m_min") != nullptr);
  CHECK(strstr(url, "timezone=auto") != nullptr);
  CHECK(strstr(url, "fahrenheit") == nullptr);
  weatherUrl(url, sizeof url, -33.8688f, 151.2093f, 'F');
  CHECK(strstr(url, "latitude=-33.8688&longitude=151.2093") != nullptr);
  CHECK(strstr(url, "&temperature_unit=fahrenheit") != nullptr);
  char tiny[20];
  CHECK(weatherUrl(tiny, sizeof tiny, 1.f, 2.f, 'C') >= (int)sizeof tiny);   // reports truncation, never overflows

  return finish();
}
