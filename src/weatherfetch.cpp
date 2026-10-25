#include "weatherfetch.h"
#include <ArduinoJson.h>
#include <time.h>
#include "settings.h"
#include "tls.h"

bool weatherFetch(Weather& out, String& err) {
  if (!weatherCoordsOk(gCfg.wLat, gCfg.wLon)) { err = "no location chosen"; return false; }
  char url[260];
  if (weatherUrl(url, sizeof url, gCfg.wLat, gCfg.wLon, gCfg.wUnit) >= (int)sizeof url) { err = "URL too long"; return false; }

  WiFiClientSecure client;
  tlsConfigure(client);
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) { err = "bad URL"; return false; }
  int code = http.GET();
  if (code <= 0) { err = connectFailText(code, client); http.end(); return false; }
  String body = http.getString();
  http.end();
  if (body.length() > 4000) { err = "response too large"; return false; }

  // keep only what is used: the reply carries units and metadata we do not need
  JsonDocument filter;
  filter["error"] = true;
  filter["reason"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["daily"]["temperature_2m_max"][0] = true;
  filter["daily"]["temperature_2m_min"][0] = true;

  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (je) { err = String("bad JSON: ") + je.c_str(); return false; }
  if (code != 200 || (doc["error"] | false)) {
    // Open-Meteo explains rejected requests in "reason"
    err = String("HTTP ") + code + ": " + (const char*)(doc["reason"] | "rejected");
    return false;
  }
  if (!doc["current"]["temperature_2m"].is<float>()) { err = "reply had no temperature"; return false; }

  Weather w{};
  w.valid = true;
  w.unit = gCfg.wUnit;
  w.temp = weatherRound(doc["current"]["temperature_2m"].as<float>());
  w.code = (uint8_t)(doc["current"]["weather_code"] | 0);
  w.isDay = (doc["current"]["is_day"] | 1) != 0;
  w.hi = doc["daily"]["temperature_2m_max"][0].is<float>() ? weatherRound(doc["daily"]["temperature_2m_max"][0].as<float>()) : w.temp;
  w.lo = doc["daily"]["temperature_2m_min"][0].is<float>() ? weatherRound(doc["daily"]["temperature_2m_min"][0].as<float>()) : w.temp;
  w.fetched = (uint32_t)time(nullptr);
  out = w;
  return true;
}
