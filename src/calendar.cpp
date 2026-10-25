#include "calendar.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "settings.h"

#ifndef BUDDY_TLS_INSECURE
// Certificate bundle embedded in the Arduino-ESP32 core (Mozilla roots). If your build lacks it,
// the link step fails on this symbol: add -DBUDDY_TLS_INSECURE to build_flags (see README).
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

bool calendarFetch(Agenda& out, String& err) {
  if (gCfg.calUrl.isEmpty()) { err = "no calendar URL"; return false; }

  WiFiClientSecure client;
#ifdef BUDDY_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACertBundle(rootca_crt_bundle_start);
#endif
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // Apps Script answers with a 302 to googleusercontent.com
  http.setTimeout(12000);
  if (!http.begin(client, gCfg.calUrl)) { err = "bad URL"; return false; }
  int code = http.GET();
  if (code != 200) {
    err = code > 0 ? String("HTTP ") + code : String("connect failed (") + HTTPClient::errorToString(code) + ")";
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.length() > 6000) { err = "response too large"; return false; }

  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, body);
  if (je) { err = String("bad JSON: ") + je.c_str(); return false; }
  if (!(doc["ok"] | false)) { err = String("script said: ") + (const char*)(doc["error"] | "unknown error"); return false; }

  memset(&out, 0, sizeof(out));
  for (JsonObject e : doc["events"].as<JsonArray>()) {
    if (out.n >= AGENDA_MAX) break;
    uint32_t s = e["s"] | 0u, en = e["e"] | 0u;
    if (s == 0 || en < s) continue;
    Event& ev = out.ev[out.n++];
    agendaSanitize((const char*)(e["t"] | "(no title)"), ev.title, sizeof ev.title);
    if (!ev.title[0]) strlcpy(ev.title, "Busy", sizeof ev.title);
    ev.start = s;
    ev.end = en;
  }
  agendaSort(out);
  out.valid = true;
  out.fetched = (uint32_t)time(nullptr);
  return true;
}
