#include "helper_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "settings.h"

static bool     lastOk = false;
static IPAddress cachedIp;
static uint32_t cachedAt = 0;
static String   cachedFor;
static char     lastErr[72] = "";

bool helperConfigured() { return !gCfg.helperHost.isEmpty(); }
bool helperLastOk() { return lastOk; }
const char* helperLastError() { return lastErr; }

// Accepts an IPv4 address or a Bonjour name like "Sachins-MacBook.local".
static bool resolve(IPAddress& out) {
  const String& h = gCfg.helperHost;
  if (h.isEmpty()) return false;
  if (out.fromString(h)) return true;
  if (cachedFor == h && cachedIp != IPAddress() && millis() - cachedAt < 10UL * 60UL * 1000UL) { out = cachedIp; return true; }
  String name = h;
  if (name.endsWith(".local")) name.remove(name.length() - 6);
  IPAddress ip = MDNS.queryHost(name, 2000);               // blocks up to 2 s; we are on core 0
  if (ip == IPAddress()) return false;
  cachedIp = ip; cachedAt = millis(); cachedFor = h;
  out = ip;
  return true;
}

static bool post(const char* path, const String& body) {
  lastErr[0] = 0;
  if (WiFi.status() != WL_CONNECTED) { lastOk = false; snprintf(lastErr, sizeof lastErr, "the buddy is not on WiFi"); return false; }
  IPAddress ip;
  if (!resolve(ip)) { lastOk = false; snprintf(lastErr, sizeof lastErr, "cannot find '%.40s' (Mac asleep, or not on this WiFi?)", gCfg.helperHost.c_str()); return false; }
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(ip.toString(), gCfg.helperPort, path)) { lastOk = false; snprintf(lastErr, sizeof lastErr, "bad helper address"); return false; }
  http.addHeader("Authorization", String("Bearer ") + gCfg.apiToken);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  if (code <= 0) cachedFor = "";                           // connection failed: resolve again next time
  lastOk = (code == 200);
  if (code <= 0)         snprintf(lastErr, sizeof lastErr, "no answer from %s:%u (is the helper running?)", ip.toString().c_str(), (unsigned)gCfg.helperPort);
  else if (code == 401)  snprintf(lastErr, sizeof lastErr, "helper refused the token (copy it again into helper/config.json)");
  else if (code == 429)  snprintf(lastErr, sizeof lastErr, "helper is rate-limiting or locked out for now");
  else if (code != 200)  snprintf(lastErr, sizeof lastErr, "helper answered HTTP %d", code);
  return lastOk;
}

bool helperAction(const char* id) {
  JsonDocument d;
  d["id"] = id;
  String body;
  serializeJson(d, body);
  return post("/action", body);
}

bool helperNotify(const char* title, const char* text) {
  JsonDocument d;
  d["title"] = title;
  d["body"] = text;
  String body;
  serializeJson(d, body);
  return post("/notify", body);
}
