#include "web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <new>
#include <time.h>
#include "faceids.h"
#include "helper_client.h"
#include "hid.h"
#include "keycombo.h"
#include "layout.h"
#include "log.h"
#include "net.h"
#include "photostore.h"
#include "notetext.h"
#include "power.h"
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
  if (server.hasHeader("Authorization") && ++authFails >= 6) { lockUntil = millis() + 30000; authFails = 0; logEvent("web: 6 wrong passwords in a row, locked out for 30 s"); }
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
  {
    Weather w;
    portENTER_CRITICAL(&gMux); w = gWeather; portEXIT_CRITICAL(&gMux);
    JsonObject wx = d["wx"].to<JsonObject>();
    wx["state"] = calStateName(n.wxState);          // off | pending | ok | error
    wx["err"] = n.wxErr;
    if (w.valid) {
      char unit[2] = { w.unit, 0 };
      wx["temp"] = w.temp; wx["hi"] = w.hi; wx["lo"] = w.lo;
      wx["unit"] = unit;
      wx["words"] = weatherWords(w.code, w.isDay);
      wx["fetched"] = w.fetched;
    }
  }
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
  d["face"] = gCfg.face;
  {
    char ac[8];
    colorFormat(gCfg.accent, ac);
    d["accent"] = ac;
  }
  d["photoDim"] = gCfg.photoDim;
  d["hasPhoto"] = photoPresent();
  d["photoVer"] = photoCurrent().ver;
  JsonArray faces = d["faces"].to<JsonArray>();               // the web editor builds its picker from this list
  for (int i = 0; i < FACE_COUNT; i++) {
    JsonObject o = faces.add<JsonObject>();
    o["id"] = i;
    o["key"] = faceKey(i);
    o["name"] = faceTitle(i);
    o["about"] = faceBlurb(i);
  }
  d["calSet"] = !gCfg.calUrl.isEmpty();          // the URL carries a secret, so it is never sent back
  d["helperHost"] = gCfg.helperHost;
  d["helperPort"] = gCfg.helperPort;
  d["weatherOn"] = gCfg.weatherOn;
  d["wLat"] = gCfg.wLat;
  d["wLon"] = gCfg.wLon;
  d["wPlace"] = gCfg.wPlace;
  d["wUnit"] = String(gCfg.wUnit);
  sendJson(d);
}

static void handleLogs() {
  if (!requireAuth()) return;
  LogEntry* lines = new (std::nothrow) LogEntry[LOG_LINES];
  if (!lines) return sendErr(503, "out of memory");
  int n = logRead(lines, LOG_LINES);
  JsonDocument d;
  d["up"] = millis() / 1000;
  d["now"] = time(nullptr) > 1735689600 ? (uint32_t)time(nullptr) : 0;
  JsonArray a = d["lines"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["u"] = lines[i].up;
    o["e"] = lines[i].epoch;
    o["m"] = lines[i].text;
  }
  delete[] lines;
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
  {
    float lat = d["wLat"].is<float>() ? d["wLat"].as<float>() : gCfg.wLat;
    float lon = d["wLon"].is<float>() ? d["wLon"].as<float>() : gCfg.wLon;
    bool on = d["weatherOn"].is<bool>() ? d["weatherOn"].as<bool>() : gCfg.weatherOn;
    if ((d["wLat"].is<float>() || d["wLon"].is<float>()) && !weatherCoordsOk(lat, lon))
      return sendErr(400, "weather location: latitude must be -90..90 and longitude -180..180");
    if (on && !weatherCoordsOk(lat, lon)) return sendErr(400, "weather: search for your city and choose it first");
    if (d["wUnit"].is<const char*>() && d["wUnit"].as<String>() != "C" && d["wUnit"].as<String>() != "F")
      return sendErr(400, "weather unit must be C or F");
    if (d["wPlace"].is<const char*>() && d["wPlace"].as<String>().length() > 60) return sendErr(400, "place name is too long");
  }
  if (d["face"].is<int>() && !faceIdOk(d["face"].as<int>())) return sendErr(400, "unknown clock face");
  if (d["accent"].is<const char*>()) {
    uint32_t ac;
    if (!colorParse(d["accent"].as<const char*>(), ac)) return sendErr(400, "colour must look like #00D1FF");
    if (!accentVisible(ac)) return sendErr(400, "choose a brighter colour: dark ones disappear on the black screen");
  }
  if (d["photoDim"].is<int>() && (d["photoDim"].as<int>() < 0 || d["photoDim"].as<int>() > PHOTO_DIM_MAX))
    return sendErr(400, "photo darkening must be 0 to 80 percent");
  if (d["helperHost"].is<const char*>() && !helperHostOk(d["helperHost"].as<String>()))
    return sendErr(400, "Mac address: use a private IP (192.168.x.x) or a name ending in .local");
  if (d["helperPort"].is<int>() && (d["helperPort"].as<int>() < 1 || d["helperPort"].as<int>() > 65535)) return sendErr(400, "port out of range");
  if (d["newAdmin"].is<const char*>() && d["newAdmin"].as<String>().length() < 6) return sendErr(400, "admin password must be at least 6 characters");

  if (d["tz"].is<const char*>() && gCfg.tz != d["tz"].as<String>()) { gCfg.tz = d["tz"].as<String>(); tzChanged = true; }
  if (d["host"].is<const char*>() && gCfg.host != d["host"].as<String>()) { gCfg.host = d["host"].as<String>(); reboot = true; }
  if (d["calUrl"].is<const char*>() && gCfg.calUrl != d["calUrl"].as<String>()) { gCfg.calUrl = d["calUrl"].as<String>(); calChanged = true; }
  bool wxChanged = false;
  if (d["weatherOn"].is<bool>() && gCfg.weatherOn != d["weatherOn"].as<bool>()) { gCfg.weatherOn = d["weatherOn"].as<bool>(); wxChanged = true; }
  if (d["wLat"].is<float>() && gCfg.wLat != d["wLat"].as<float>()) { gCfg.wLat = d["wLat"].as<float>(); wxChanged = true; }
  if (d["wLon"].is<float>() && gCfg.wLon != d["wLon"].as<float>()) { gCfg.wLon = d["wLon"].as<float>(); wxChanged = true; }
  if (d["wUnit"].is<const char*>() && gCfg.wUnit != d["wUnit"].as<String>()[0]) { gCfg.wUnit = d["wUnit"].as<String>()[0]; wxChanged = true; }
  if (d["wPlace"].is<const char*>()) {
    String pl = d["wPlace"].as<String>();
    for (size_t i = 0; i < pl.length(); i++) if ((unsigned char)pl[i] < ' ' || pl[i] == '"' || pl[i] == '\\') pl.setCharAt(i, ' ');
    gCfg.wPlace = pl;
  }
  if (d["helperHost"].is<const char*>()) gCfg.helperHost = d["helperHost"].as<String>();
  if (d["helperPort"].is<int>()) gCfg.helperPort = (uint16_t)d["helperPort"].as<int>();
  if (d["bright"].is<int>())      gCfg.bright      = (uint8_t)constrain(d["bright"].as<int>(), 10, 255);
  if (d["nightBright"].is<int>()) gCfg.nightBright = (uint8_t)constrain(d["nightBright"].as<int>(), 0, 255);
  if (d["sleepFrom"].is<int>())   gCfg.sleepFrom   = (uint8_t)constrain(d["sleepFrom"].as<int>(), 0, 23);
  if (d["sleepTo"].is<int>())     gCfg.sleepTo     = (uint8_t)constrain(d["sleepTo"].as<int>(), 0, 23);
  if (d["leadMin"].is<int>())     gCfg.leadMin     = (uint8_t)constrain(d["leadMin"].as<int>(), 0, 60);
  if (d["clockSecs"].is<int>())   gCfg.clockSecs   = (uint8_t)constrain(d["clockSecs"].as<int>(), 3, 120);
  if (d["ledOn"].is<bool>())      gCfg.ledOn = d["ledOn"].as<bool>();
  bool faceChanged = false;
  if (d["face"].is<int>() && gCfg.face != d["face"].as<int>()) { gCfg.face = (uint8_t)d["face"].as<int>(); faceChanged = true; }
  if (d["accent"].is<const char*>()) { uint32_t ac; colorParse(d["accent"].as<const char*>(), ac); gCfg.accent = ac; }
  if (d["photoDim"].is<int>())    gCfg.photoDim = photoDimClamp(d["photoDim"].as<int>());
  if (d["bleHid"].is<bool>() && gCfg.bleHid != d["bleHid"].as<bool>()) { gCfg.bleHid = d["bleHid"].as<bool>(); reboot = true; }
  if (d["newAdmin"].is<const char*>()) gCfg.adminPass = d["newAdmin"].as<String>();
  if (d["regenToken"].is<bool>() && d["regenToken"].as<bool>()) gCfg.apiToken = newToken();

  settingsSave();
  if (faceChanged) logEvent("clock face: %s", faceTitle(gCfg.face));
  logEvent("settings saved%s%s", calChanged ? " (calendar link changed)" : "", reboot ? ", restarting to apply" : "");
  if (tzChanged) netApplyTime();
  if (calChanged) netRequestCalendar();
  if (wxChanged) {
    portENTER_CRITICAL(&gMux); gWeather.valid = false; portEXIT_CRITICAL(&gMux);     // never show the old place's weather
    logEvent("weather: %s%s%s", gCfg.weatherOn ? "on for " : "off", gCfg.weatherOn ? gCfg.wPlace.c_str() : "", gCfg.weatherOn ? (gCfg.wUnit == 'F' ? " (F)" : " (C)") : "");
    netRequestWeather();
  }

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

// The Mac helper forwards Mac notifications here (Bearer token): {"app":"Slack","title":"Priya"}.
// Only the app name and the title are shown, never the message text. Rate-limited so a chatty app cannot flood the screen.
static void handleNotify() {
  if (!requireAuth(true)) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (lastMs && now - lastMs < 1500) return sendErr(429, "too many notifications, slow down");
  char app[24], title[40];
  noteClean(d["app"] | "", app, sizeof app);
  noteClean(d["title"] | "", title, 30);       // 28 pt text: about 3 lines fit above the app name
  if (!title[0] && !app[0]) return sendErr(400, "nothing to show");
  lastMs = now ? now : 1;
  netPostNote(app[0] ? app : "Notification", title[0] ? title : "New notification");
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
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { otaErr = Update.errorString(); otaAuth = false; logEvent("firmware update: cannot start: %s", otaErr.c_str()); return; }
    logEvent("firmware update: receiving the new firmware");
    netPostToast("Updating firmware...");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaAuth && Update.write(up.buf, up.currentSize) != up.currentSize) { otaErr = Update.errorString(); otaAuth = false; Update.abort(); }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaAuth && !Update.end(true)) { otaErr = Update.errorString(); otaAuth = false; logEvent("firmware update: FAILED: %s", otaErr.c_str()); }
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
  logEvent("firmware update: installed");
  netRequestReboot();
}

// ------------------------------------------------------------------ photo for the Photo clock face
// POST /api/photo takes the 240x240 RGB565 file the web editor builds (see photo.h) as a multipart upload,
// the same way the firmware update does, and authenticates before anything is stored.
static bool   photoAuth = false;
static String photoErr;

static void photoChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    photoErr = "";
    photoAuth = uploadAuthOk();
    if (!photoAuth) return;
    if (!photoUploadBegin(photoErr)) photoAuth = false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (photoAuth) photoUploadWrite(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (photoAuth && !photoUploadEnd(photoErr)) photoAuth = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (photoAuth) photoUploadAbort();
    photoAuth = false;
  }
}

static void photoDone() {
  bool ok = photoAuth;
  photoAuth = false;                                // never let a stale flag authorise a later request
  if (!ok) {
    if (!photoErr.isEmpty()) return sendErr(400, photoErr);
    return (void)requireAuth();
  }
  JsonDocument r;
  r["ok"] = true;
  r["photoVer"] = photoCurrent().ver;
  sendJson(r);
}

// GET /api/photo: the saved file, so the editor can show what the buddy shows.
static void handlePhotoGet() {
  if (!requireAuth()) return;
  if (!LittleFS.exists(PHOTO_PATH)) return sendErr(404, "no photo saved");
  File f = LittleFS.open(PHOTO_PATH, "r");
  if (!f) return sendErr(500, "could not read the photo");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handlePhotoDelete() {
  if (!requireAuth()) return;
  photoRemove();
  sendOk();
}

// ------------------------------------------------------------------ WiFi networks
// GET /api/scan: the setup page gets a plain list of names; the web editor asks for ?detail=1 and gets
// signal strength and whether a password is needed. The scan blocks the buddy for 2-4 seconds.
static void handleScan() {
  if (!netApActive() && !requireAuth()) return;
  WifiHit hits[40];
  int n = netScan(hits, 40);
  if (n < 0) return sendErr(503, "the WiFi scan failed, try again");
  if (n > 20) n = 20;
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  bool detail = server.hasArg("detail");
  for (int i = 0; i < n; i++) {
    if (!detail) { a.add(hits[i].ssid); continue; }
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = hits[i].ssid;
    o["rssi"] = hits[i].rssi;
    o["open"] = hits[i].open;
  }
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

// GET /api/wifi: the saved networks in priority order. Passwords are never sent back.
static void handleWifiList() {
  if (!requireAuth()) return;
  JsonDocument d;
  d["max"] = WIFI_MAX;
  d["current"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
  JsonArray a = d["nets"].to<JsonArray>();
  for (int i = 0; i < gCfg.wifi.n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = gCfg.wifi.net[i].ssid;
    o["hasPass"] = gCfg.wifi.net[i].pass[0] != 0;
  }
  sendJson(d);
}

// POST /api/wifi/add {"ssid": "...", "pass": "...", "front": false}
// An existing name is updated; leaving "pass" out keeps its saved password.
static void handleWifiAdd() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  String ssid = d["ssid"] | "";
  bool hasPass = d["pass"].is<const char*>();
  String pass = hasPass ? d["pass"].as<String>() : String();
  WifiResult r = wifiSet(gCfg.wifi, ssid.c_str(), hasPass ? pass.c_str() : nullptr, d["front"] | false);
  if (r != WIFI_OK) return sendErr(400, wifiResultText(r));
  settingsSave();
  netWifiListChanged();
  logEvent("wifi: saved network '%s' (%d of %d)", ssid.c_str(), (int)gCfg.wifi.n, WIFI_MAX);
  sendOk();
}

// POST /api/wifi/remove {"ssid": "..."}
static void handleWifiRemove() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  String ssid = d["ssid"] | "";
  WifiResult r = wifiRemove(gCfg.wifi, ssid.c_str());
  if (r != WIFI_OK) return sendErr(404, wifiResultText(r));
  settingsSave();
  netWifiListChanged();
  logEvent("wifi: removed network '%s' (%d left)", ssid.c_str(), (int)gCfg.wifi.n);
  sendOk();
}

// POST /api/wifi/move {"ssid": "...", "dir": -1}   -1 = try it earlier, +1 = later
static void handleWifiMove() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  String ssid = d["ssid"] | "";
  WifiResult r = wifiMove(gCfg.wifi, ssid.c_str(), d["dir"] | 0);
  if (r != WIFI_OK) return sendErr(404, wifiResultText(r));
  settingsSave();
  netWifiListChanged();
  sendOk();
}

// POST /api/wifi/connect {"ssid": "..."}: leave the current network and join this saved one now. If it cannot be
// joined the buddy carries on through the saved networks by itself, so it is never left without WiFi.
static void handleWifiConnect() {
  if (!requireAuth()) return;
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) return sendErr(400, "invalid JSON");
  String ssid = d["ssid"] | "";
  if (wifiFind(gCfg.wifi, ssid.c_str()) < 0) return sendErr(404, wifiResultText(WIFI_NOT_FOUND));
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) return sendErr(400, "the buddy is already on that network");
  sendOk();                                     // the reply goes out first: switching drops this very connection
  netRequestWifiSwitch(ssid.c_str());
}

// POST /api/wifi (setup page, also usable from the editor): save this network as the first choice and restart.
static void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass"), admin = server.arg("admin");
  String host = server.arg("host"), tz = server.arg("tz");
  if (ssid.isEmpty() || ssid.length() > 32) return sendErr(400, "choose a network");
  if (gCfg.adminPass.isEmpty()) {
    if (admin.length() < 6) return sendErr(400, "admin password must be at least 6 characters");
  } else if (!sameToken(admin, gCfg.adminPass)) {
    return sendErr(403, "wrong admin password");
  }
  if (!host.isEmpty() && !hostNameOk(host)) return sendErr(400, "name: letters, digits and - only");
  if (!tz.isEmpty() && !tzOk(tz)) return sendErr(400, "bad timezone string");
  WifiList trial = gCfg.wifi;                    // validate (and make room) before changing anything
  WifiResult r = wifiSet(trial, ssid.c_str(), pass.c_str(), true);
  if (r == WIFI_FULL) {                          // setup always wins: drop the lowest-priority network
    trial.n = WIFI_MAX - 1;
    memset(&trial.net[WIFI_MAX - 1], 0, sizeof trial.net[WIFI_MAX - 1]);
    r = wifiSet(trial, ssid.c_str(), pass.c_str(), true);
  }
  if (r != WIFI_OK) return sendErr(400, wifiResultText(r));

  if (gCfg.adminPass.isEmpty()) gCfg.adminPass = admin;
  if (!host.isEmpty()) gCfg.host = host;
  if (!tz.isEmpty()) gCfg.tz = tz;
  gCfg.wifi = trial;
  settingsSave();
  logEvent("wifi: network '%s' saved as the first choice from the setup page", ssid.c_str());
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
  server.on("/api/wifi", HTTP_GET, handleWifiList);
  server.on("/api/wifi/add", HTTP_POST, handleWifiAdd);
  server.on("/api/wifi/remove", HTTP_POST, handleWifiRemove);
  server.on("/api/wifi/move", HTTP_POST, handleWifiMove);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/layout", HTTP_GET, handleLayoutGet);
  server.on("/api/token", HTTP_GET, handleToken);
  server.on("/api/agenda", HTTP_GET, handleAgenda);
  server.on("/api/logs", HTTP_GET, handleLogs);

  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/layout", HTTP_POST, handleLayoutPost);
  server.on("/api/layout/reset", HTTP_POST, handleLayoutReset);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/stats", HTTP_POST, handleStats);
  server.on("/api/notify", HTTP_POST, handleNotify);
  server.on("/api/weather/refresh", HTTP_POST, []() { if (!requireAuth()) return; netRequestWeather(); sendOk(); });
  server.on("/api/calendar/refresh", HTTP_POST, []() { if (!requireAuth()) return; netRequestCalendar(); sendOk(); });
  server.on("/api/power/off", HTTP_POST, []() { if (!requireAuth()) return; sendOk(); powerRequestOff(); });
  server.on("/api/reboot", HTTP_POST, []() { if (!requireAuth()) return; sendOk(); netRequestReboot(); });
  server.on("/update", HTTP_POST, otaDone, otaChunk);
  server.on("/api/photo", HTTP_GET, handlePhotoGet);
  server.on("/api/photo", HTTP_POST, photoDone, photoChunk);
  server.on("/api/photo/delete", HTTP_POST, handlePhotoDelete);

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
