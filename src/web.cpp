#include "web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <time.h>
#include "helper_client.h"
#include "hid.h"
#include "keycombo.h"
#include "layout.h"
#include "net.h"
#include "settings.h"
#include "setup_page.h"
#include "state.h"

static WebServer server(80);

// ------------------------------------------------------------------ auth
//
// Two ways in:
//   Basic admin:<password>   the web editor in a browser
//   Bearer <api token>       the Mac helper (only for the few endpoints that allow it)
//
// Browsers attach cached Basic credentials to cross-site requests, so every state-changing request
// that relies on Basic auth must also carry an X-Buddy header. A web page on another origin cannot add
// that header without a CORS preflight, and this server never answers preflights.
enum AuthKind { A_NONE, A_BASIC, A_BEARER };

static uint8_t  authFails = 0;
static uint32_t lockUntil = 0;

static bool sameToken(const String& a, const String& b) {      // constant time for equal lengths
  if (a.length() != b.length() || a.isEmpty()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static AuthKind checkAuth() {
  String h = server.header("Authorization");
  if (h.startsWith("Bearer ")) return sameToken(h.substring(7), gCfg.apiToken) ? A_BEARER : A_NONE;
  if (!gCfg.adminPass.isEmpty() && server.authenticate("admin", gCfg.adminPass.c_str())) return A_BASIC;
  return A_NONE;
}

static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}
static void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }
static void sendErr(int code, const String& msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  sendJson(d, code);
}

static bool requireAuth(bool allowBearer = false) {
  if ((int32_t)(millis() - lockUntil) < 0) { sendErr(429, "too many failed attempts, wait 30 seconds"); return false; }
  AuthKind k = checkAuth();
  if (k == A_BEARER && allowBearer) { authFails = 0; return true; }
  if (k == A_BASIC) {
    if (server.method() == HTTP_POST && !server.hasHeader("X-Buddy")) { sendErr(403, "missing X-Buddy header"); return false; }
    authFails = 0;
    return true;
  }
  if (server.hasHeader("Authorization") && ++authFails >= 6) { lockUntil = millis() + 30000; authFails = 0; }
  if (gCfg.adminPass.isEmpty() && !allowBearer) sendErr(403, "no admin password set - redo the WiFi setup");
  else server.requestAuthentication(BASIC_AUTH, "Buddy");
  return false;
}

// Uploads are authenticated inside the upload callback, before any data is written.
static bool uploadAuthOk() {
  return checkAuth() == A_BASIC && server.hasHeader("X-Buddy");
}

// ------------------------------------------------------------------ static files
static const char* mimeFor(const String& p) {
  if (p.endsWith(".html")) return "text/html";
  if (p.endsWith(".js"))   return "application/javascript";
  if (p.endsWith(".css"))  return "text/css";
  if (p.endsWith(".json")) return "application/json";
  if (p.endsWith(".svg"))  return "image/svg+xml";
  if (p.endsWith(".png"))  return "image/png";
  return "application/octet-stream";
}

static bool serveFile(const String& path) {
  if (path.indexOf("..") >= 0 || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) return false;
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, mimeFor(path));
  f.close();
  return true;
}

// ------------------------------------------------------------------ validation helpers
static bool hostNameOk(const String& h) {
  if (h.length() < 1 || h.length() > 24) return false;
  for (size_t i = 0; i < h.length(); i++) if (!(isalnum((unsigned char)h[i]) || h[i] == '-')) return false;
  return true;
}

// The Mac helper address receives the API token, so only accept places on the local network:
// a private IPv4 address, or a ".local" Bonjour name.
static bool helperHostOk(const String& h) {
  if (h.isEmpty()) return true;                       // empty = feature off
  if (h.length() > 63) return false;
  IPAddress ip;
  if (ip.fromString(h)) {
    return ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168) ||
           ip[0] == 127 || (ip[0] == 169 && ip[1] == 254);
  }
  if (!h.endsWith(".local") || h.length() < 8) return false;
  for (size_t i = 0; i < h.length(); i++) if (!(isalnum((unsigned char)h[i]) || h[i] == '-' || h[i] == '.' || h[i] == '_')) return false;
  return true;
}

static bool calUrlOk(const String& u) {
  if (u.isEmpty()) return true;                       // empty = feature off
  if (u.length() > 300 || !u.startsWith("https://script.google.com/macros/s/")) return false;
  for (size_t i = 0; i < u.length(); i++) if ((unsigned char)u[i] <= ' ' || u[i] == '"' || u[i] == '\\') return false;
  return true;
}

static bool tzOk(const String& t) {
  if (t.length() < 3 || t.length() > 63) return false;
  for (size_t i = 0; i < t.length(); i++) if ((unsigned char)t[i] < ' ' || t[i] > '~' || t[i] == '"') return false;
  return true;
}

static String newToken() {
  char buf[25];
  for (int i = 0; i < 3; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
  return String(buf);
}

// ------------------------------------------------------------------ read-only API
static const char* calStateName(uint8_t s) {
  switch (s) {
    case CAL_PENDING: return "pending";
    case CAL_OK:      return "ok";
    case CAL_ERROR:   return "error";
    default:          return "off";
  }
}

static void handleStatus() {
  NetStatus n;
  portENTER_CRITICAL(&gMux); n = gNet; portEXIT_CRITICAL(&gMux);
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["host"] = gCfg.host;
  d["ip"] = n.ip;
  d["ssid"] = n.ssid;
  d["rssi"] = n.rssi;
  d["apMode"] = n.apMode;
  d["ble"] = n.bleConnected;
  d["bleEnabled"] = gCfg.bleHid;
  d["helperSet"] = n.helperSet;
  d["helperOk"] = n.helperOk;
  d["cal"] = calStateName(n.calState);
  d["calErr"] = n.calErr;
  d["timeSynced"] = n.timeSynced;
  d["now"] = n.timeSynced ? (uint32_t)time(nullptr) : 0;
  d["uptime"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["fsUsed"] = LittleFS.usedBytes();
  d["fsTotal"] = LittleFS.totalBytes();
  d["authSet"] = !gCfg.adminPass.isEmpty();
  sendJson(d);
}

static void handleSettingsGet() {
  JsonDocument d;
  d["host"] = gCfg.host;
  d["tz"] = gCfg.tz;
  d["bright"] = gCfg.bright;
  d["nightBright"] = gCfg.nightBright;
  d["sleepFrom"] = gCfg.sleepFrom;
  d["sleepTo"] = gCfg.sleepTo;
  d["leadMin"] = gCfg.leadMin;
  d["clockSecs"] = gCfg.clockSecs;
  d["ledOn"] = gCfg.ledOn;
  d["bleHid"] = gCfg.bleHid;
  d["calSet"] = !gCfg.calUrl.isEmpty();          // the URL carries a secret, so it is never sent back
  d["helperHost"] = gCfg.helperHost;
  d["helperPort"] = gCfg.helperPort;
  sendJson(d);
}

static void handleToken() {
  if (!requireAuth()) return;
  JsonDocument d;
  d["token"] = gCfg.apiToken;
  d["host"] = gCfg.host;
  sendJson(d);
}

static void handleAgenda() {
  if (!requireAuth(true)) return;
  Agenda a;
  portENTER_CRITICAL(&gMux); a = gAgenda; portEXIT_CRITICAL(&gMux);
  JsonDocument d;
  d["valid"] = a.valid;
  d["fetched"] = a.fetched;
  JsonArray arr = d["events"].to<JsonArray>();
  for (int i = 0; i < a.n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["t"] = a.ev[i].title;
    o["s"] = a.ev[i].start;
    o["e"] = a.ev[i].end;
  }
  sendJson(d);
}

static void handleLayoutGet() {
  String json = settingsLoadLayoutJson();
  Layout probe;
  String err;
  if (json.isEmpty() || !layoutParse(json, probe, err)) json = layoutDefaultJson();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// ------------------------------------------------------------------ authenticated writes
static void handleLayoutPost() {
  if (!requireAuth()) return;
  String body = server.arg("plain");
  Layout probe;
  String err;
  if (!layoutParse(body, probe, err)) return sendErr(400, err);
  settingsSaveLayoutJson(body);
  netPostLayout();
  sendOk();
}

static void handleLayoutReset() {
  if (!requireAuth()) return;
  settingsSaveLayoutJson("");
  netPostLayout();
  sendOk();
}

static void handleSettingsPost() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  bool reboot = false, tzChanged = false, calChanged = false;

  // validate everything first so a bad field never leaves half of the settings applied
  if (d["tz"].is<const char*>() && !tzOk(d["tz"].as<String>())) return sendErr(400, "timezone: use a POSIX string such as IST-5:30");
  if (d["host"].is<const char*>() && !hostNameOk(d["host"].as<String>())) return sendErr(400, "name: letters, digits and - only (max 24)");
  if (d["calUrl"].is<const char*>() && !calUrlOk(d["calUrl"].as<String>()))
    return sendErr(400, "calendar URL must start with https://script.google.com/macros/s/");
  if (d["helperHost"].is<const char*>() && !helperHostOk(d["helperHost"].as<String>()))
    return sendErr(400, "Mac address: use a private IP (192.168.x.x) or a name ending in .local");
  if (d["helperPort"].is<int>() && (d["helperPort"].as<int>() < 1 || d["helperPort"].as<int>() > 65535)) return sendErr(400, "port out of range");
  if (d["newAdmin"].is<const char*>() && d["newAdmin"].as<String>().length() < 6) return sendErr(400, "admin password must be at least 6 characters");

  if (d["tz"].is<const char*>() && gCfg.tz != d["tz"].as<String>()) { gCfg.tz = d["tz"].as<String>(); tzChanged = true; }
  if (d["host"].is<const char*>() && gCfg.host != d["host"].as<String>()) { gCfg.host = d["host"].as<String>(); reboot = true; }
  if (d["calUrl"].is<const char*>() && gCfg.calUrl != d["calUrl"].as<String>()) { gCfg.calUrl = d["calUrl"].as<String>(); calChanged = true; }
  if (d["helperHost"].is<const char*>()) gCfg.helperHost = d["helperHost"].as<String>();
  if (d["helperPort"].is<int>()) gCfg.helperPort = (uint16_t)d["helperPort"].as<int>();
  if (d["bright"].is<int>())      gCfg.bright      = (uint8_t)constrain(d["bright"].as<int>(), 10, 255);
  if (d["nightBright"].is<int>()) gCfg.nightBright = (uint8_t)constrain(d["nightBright"].as<int>(), 0, 255);
  if (d["sleepFrom"].is<int>())   gCfg.sleepFrom   = (uint8_t)constrain(d["sleepFrom"].as<int>(), 0, 23);
  if (d["sleepTo"].is<int>())     gCfg.sleepTo     = (uint8_t)constrain(d["sleepTo"].as<int>(), 0, 23);
  if (d["leadMin"].is<int>())     gCfg.leadMin     = (uint8_t)constrain(d["leadMin"].as<int>(), 0, 60);
  if (d["clockSecs"].is<int>())   gCfg.clockSecs   = (uint8_t)constrain(d["clockSecs"].as<int>(), 3, 120);
  if (d["ledOn"].is<bool>())      gCfg.ledOn = d["ledOn"].as<bool>();
  if (d["bleHid"].is<bool>() && gCfg.bleHid != d["bleHid"].as<bool>()) { gCfg.bleHid = d["bleHid"].as<bool>(); reboot = true; }
  if (d["newAdmin"].is<const char*>()) gCfg.adminPass = d["newAdmin"].as<String>();
  if (d["regenToken"].is<bool>() && d["regenToken"].as<bool>()) gCfg.apiToken = newToken();

  settingsSave();
  if (tzChanged) netApplyTime();
  if (calChanged) netRequestCalendar();

  JsonDocument r;
  r["ok"] = true;
  r["rebootNeeded"] = reboot;
  sendJson(r);
}

// Runs one shortcut or helper action right now, so the editor can offer a "Test" button.
static void handleTest() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  String hid = d["hid"] | "", mac = d["mac"] | "";
  if (!hid.isEmpty()) {
    Step steps[MAX_STEPS];
    char e[48];
    if (parseSequence(hid.c_str(), steps, MAX_STEPS, e, sizeof e) == 0) return sendErr(400, e);
    if (!gCfg.bleHid) return sendErr(409, "Bluetooth keyboard is switched off in Settings");
    if (!hidConnected()) return sendErr(409, "Mac is not paired over Bluetooth yet (pair 'CrowPanel Buddy' in System Settings)");
    if (!hidRun(hid.c_str())) return sendErr(500, "shortcut failed");
    return sendOk();
  }
  if (!mac.isEmpty()) {
    if (!helperConfigured()) return sendErr(409, "set the Mac address under Mac helper first");
    if (!helperAction(mac.c_str())) return sendErr(502, "the Mac helper did not accept the action (running? token matches? id exists in actions.json?)");
    return sendOk();
  }
  sendErr(400, "send {\"hid\":...} or {\"mac\":...}");
}

// The Mac helper pushes CPU and RAM here (Bearer token) so the buddy can get cranky when the Mac is busy.
static void handleStats() {
  if (!requireAuth(true)) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  portENTER_CRITICAL(&gMux);
  gMac.cpu = constrain((float)(d["cpu"] | 0.0f), 0.0f, 100.0f);
  gMac.ram = constrain((float)(d["ram"] | 0.0f), 0.0f, 100.0f);
  gMac.updatedMs = millis() ? millis() : 1;
  portEXIT_CRITICAL(&gMux);
  sendOk();
}

// ------------------------------------------------------------------ firmware update (OTA)
static bool   otaAuth = false;
static String otaErr;

static void otaChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaErr = "";
    otaAuth = uploadAuthOk();
    if (!otaAuth) return;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { otaErr = Update.errorString(); otaAuth = false; return; }
    netPostToast("Updating firmware...");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaAuth && Update.write(up.buf, up.currentSize) != up.currentSize) { otaErr = Update.errorString(); otaAuth = false; Update.abort(); }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaAuth && !Update.end(true)) { otaErr = Update.errorString(); otaAuth = false; }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (otaAuth) Update.abort();
    otaAuth = false;
  }
}

static void otaDone() {
  bool ok = otaAuth;
  otaAuth = false;                                  // never let a stale flag authorise a later request
  if (!ok) {
    if (!otaErr.isEmpty()) return sendErr(500, "update failed: " + otaErr);
    return (void)requireAuth();
  }
  server.send(200, "application/json", "{\"ok\":true}");
  netRequestReboot();
}

// ------------------------------------------------------------------ WiFi setup portal
static void handleScan() {
  if (!netApActive() && !requireAuth()) return;
  int n = WiFi.scanNetworks();
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length()) a.add(s);
  }
  WiFi.scanDelete();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass"), admin = server.arg("admin");
  String host = server.arg("host"), tz = server.arg("tz");
  if (ssid.isEmpty() || ssid.length() > 32) return sendErr(400, "choose a network");
  if (pass.length() > 63) return sendErr(400, "WiFi password too long");
  if (gCfg.adminPass.isEmpty()) {
    if (admin.length() < 6) return sendErr(400, "admin password must be at least 6 characters");
  } else if (!sameToken(admin, gCfg.adminPass)) {
    return sendErr(403, "wrong admin password");
  }
  if (!host.isEmpty() && !hostNameOk(host)) return sendErr(400, "name: letters, digits and - only");
  if (!tz.isEmpty() && !tzOk(tz)) return sendErr(400, "bad timezone string");

  if (gCfg.adminPass.isEmpty()) gCfg.adminPass = admin;
  if (!host.isEmpty()) gCfg.host = host;
  if (!tz.isEmpty()) gCfg.tz = tz;
  gCfg.ssid = ssid;
  gCfg.pass = pass;
  settingsSave();
  sendOk();
  netRequestReboot();
}

// ------------------------------------------------------------------ routing
void webInit() {
  const char* keys[] = {"Authorization", "X-Buddy"};
  server.collectHeaders(keys, 2);

  server.on("/", HTTP_GET, []() {
    if (netApActive()) {
      server.send_P(200, "text/html", SETUP_HTML);
    } else if (!serveFile("/www/index.html")) {
      server.send_P(200, "text/html", NO_FS_HTML);
    }
  });
  server.on("/setup", HTTP_GET, []() { server.send_P(200, "text/html", SETUP_HTML); });
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/layout", HTTP_GET, handleLayoutGet);
  server.on("/api/token", HTTP_GET, handleToken);
  server.on("/api/agenda", HTTP_GET, handleAgenda);

  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/layout", HTTP_POST, handleLayoutPost);
  server.on("/api/layout/reset", HTTP_POST, handleLayoutReset);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/stats", HTTP_POST, handleStats);
  server.on("/api/calendar/refresh", HTTP_POST, []() { if (!requireAuth()) return; netRequestCalendar(); sendOk(); });
  server.on("/api/reboot", HTTP_POST, []() { if (!requireAuth()) return; sendOk(); netRequestReboot(); });
  server.on("/update", HTTP_POST, otaDone, otaChunk);

  server.onNotFound([]() {
    if (netApActive()) {                       // captive-portal redirect
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
      return;
    }
    if (server.method() == HTTP_GET && serveFile("/www" + server.uri())) return;
    server.send(404, "text/plain", "not found");
  });

  server.begin();
}

void webLoop() { server.handleClient(); }
