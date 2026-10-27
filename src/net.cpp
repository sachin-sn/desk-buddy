#include "net.h"
#include "photostore.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <time.h>
#include "calendar.h"
#include "helper_client.h"
#include "hid.h"
#include "layout.h"
#include "log.h"
#include "settings.h"
#include "state.h"
#include "weatherfetch.h"
#include "web.h"

static DNSServer dns;
static bool     apActive = false;
static uint32_t rebootAt = 0;
static bool     mdnsUp = false;
static char     apPin[12] = "";

// WiFi roaming: the saved networks in the order to try them (see wifilist.h)
static uint8_t  wOrder[WIFI_MAX];
static uint8_t  wOrderN = 0, wTry = 0;
static uint32_t wTryAt = 0;                  // millis() when the current join attempt started
static uint32_t wScanAt = 0;                 // millis() of the last scan
static uint32_t wLostAt = 0;                 // millis() since when we have had no WiFi (0 = connected)
static char     wSwitchTo[WIFI_SSID_MAX + 1] = "";
static bool     wSwitchPending = false;

static bool     calWanted = true;            // fetch as soon as WiFi and time are ready
static uint32_t calNextAt = 0;               // millis() of the next scheduled fetch
static uint8_t  calState = CAL_NOT_SET;
static char     calErrText[96] = "";
static bool     wxWanted = true;
static uint32_t wxNextAt = 0;
static uint8_t  wxState = CAL_NOT_SET;
static char     wxErrText[80] = "";
static const uint32_t WX_EVERY_MS = 15UL * 60UL * 1000UL;
static const uint32_t WX_RETRY_MS = 2UL * 60UL * 1000UL;
static const uint32_t CAL_EVERY_MS = 5UL * 60UL * 1000UL;
static const uint32_t CAL_RETRY_MS = 60UL * 1000UL;

// ------------------------------------------------------------------ helpers shared with web.cpp
void netPostLayout() {
  UiMsg m{};
  m.type = UI_LAYOUT;
  m.layout = layoutLoad();
  if (xQueueSend(qToUi, &m, pdMS_TO_TICKS(50)) != pdTRUE) delete m.layout;     // queue full: drop, don't leak
}

void netPostToast(const char* text, bool error) {
  UiMsg m{};
  m.type = error ? UI_ERROR : UI_TOAST;
  strlcpy(m.text, text, sizeof(m.text));
  xQueueSend(qToUi, &m, 0);
}

void netPostNote(const char* app, const char* title) {
  UiMsg m{};
  m.type = UI_NOTE;
  strlcpy(m.text, title, sizeof(m.text));
  strlcpy(m.text2, app, sizeof(m.text2));
  xQueueSend(qToUi, &m, 0);                    // queue full: this one is dropped, the next gets through
}

void netApplyTime() {
  configTzTime(gCfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void netRequestReboot()   { rebootAt = millis() + 800; logEvent("restarting in a moment"); }
void netRequestCalendar() { calWanted = true; }
void netRequestWeather()  { wxWanted = true; }
void netWifiListChanged() { wOrderN = 0; }
void netRequestWifiSwitch(const char* ssid) {
  strlcpy(wSwitchTo, ssid, sizeof wSwitchTo);
  wSwitchPending = true;
}
bool netApActive()        { return apActive; }

// ------------------------------------------------------------------ WiFi
static void startAp() {
  if (apActive) return;
  snprintf(apPin, sizeof(apPin), "%08u", (unsigned)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP_STA);                  // keep trying the saved network in the background
  WiFi.softAP("CrowPanel-Buddy", apPin);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  logEvent("wifi: setup network 'CrowPanel-Buddy' is up (password is on the buddy's screen)");
}

static void stopAp() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  logEvent("wifi: setup network stopped");
}

static const char* wifiReasonText(uint8_t r) {
  switch (r) {
    case 2:   return "authentication expired";
    case 8:   return "left the network (we switched, or the router dropped us)";
    case 15:  return "4-way handshake timed out: wrong password?";
    case 200: return "router stopped answering";
    case 201: return "network not found: out of range, or 5 GHz only?";
    case 202: return "authentication failed: wrong password?";
    case 203: return "association failed";
    case 204: return "handshake timed out";
    case 205: return "connection failed";
    default:  return "see ESP32 WiFi reason codes";
  }
}

// Runs on the WiFi event task. Logs why the router link dropped, but only when the reason changes
// or every 30 s, so a missing router does not flood the log.
static void onWifiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
  if (ev != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  static uint8_t lastReason = 0;
  static uint32_t lastAt = 0;
  uint8_t r = info.wifi_sta_disconnected.reason;
  if (r != lastReason || millis() - lastAt > 30000) {
    lastReason = r;
    lastAt = millis();
    logEvent("wifi: not connected, reason %u (%s)", (unsigned)r, wifiReasonText(r));
  }
}

// Scan for access points (blocks for 2-4 s): one entry per network name, strongest first. Returns how many
// were stored (at most maxHits), or -1 if the scan failed.
int netScan(WifiHit* hits, int maxHits) {
  int n = WiFi.scanNetworks(false, false);
  if (n < 0) { WiFi.scanDelete(); return -1; }
  int k = 0;
  for (int i = 0; i < n && k < maxHits; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty() || s.length() > WIFI_SSID_MAX) continue;
    memset(&hits[k], 0, sizeof hits[k]);
    strncpy(hits[k].ssid, s.c_str(), WIFI_SSID_MAX);
    hits[k].rssi = (int8_t)WiFi.RSSI(i);
    hits[k].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    k++;
  }
  WiFi.scanDelete();
  return wifiTidyHits(hits, k);
}

// Decide the order to try the saved networks in. With only one saved network there is nothing to choose,
// so it is not scanned for.
static void wifiPlan(bool rescan) {
  WifiHit hits[40];
  int nh = 0;
  if (rescan && gCfg.wifi.n > 1) {
    nh = netScan(hits, 40);
    wScanAt = millis();
    if (nh < 0) { logEvent("wifi: scan failed, trying the saved networks in list order"); nh = 0; }
  }
  wOrderN = (uint8_t)wifiOrder(gCfg.wifi, nh > 0 ? hits : nullptr, nh, wOrder);
  wTry = 0;
  if (gCfg.wifi.n > 1 && nh > 0) {
    int seen = 0;
    for (int i = 0; i < gCfg.wifi.n; i++) for (int h = 0; h < nh; h++) if (strcmp(hits[h].ssid, gCfg.wifi.net[i].ssid) == 0) { seen++; break; }
    static int lastNh = -1, lastSeen = -1;                       // the same answer every minute is not news
    if (nh != lastNh || seen != lastSeen) {
      lastNh = nh; lastSeen = seen;
      logEvent("wifi: %d network(s) in range, %d of %d saved ones among them", nh, seen, (int)gCfg.wifi.n);
    }
  }
}

static void wifiBegin(uint8_t idx) {
  const WifiNet& w = gCfg.wifi.net[idx];
  static char lastJoin[WIFI_SSID_MAX + 1] = "";
  static uint32_t lastJoinLog = 0;
  if (strcmp(lastJoin, w.ssid) != 0 || millis() - lastJoinLog > 300000) {      // a missing router must not fill the log
    strlcpy(lastJoin, w.ssid, sizeof lastJoin);
    lastJoinLog = millis();
    logEvent("wifi: joining '%s'", w.ssid);
  }
  WiFi.begin(w.ssid, w.pass[0] ? w.pass : nullptr);          // no password = open network
  wTryAt = millis();
}

static void wifiBoot() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(gCfg.host.c_str());
  WiFi.onEvent(onWifiEvent);
  if (gCfg.wifi.n == 0) { logEvent("wifi: no network saved yet, starting setup"); startAp(); return; }
  WiFi.mode(WIFI_STA);
  wifiPlan(true);
  // Try the saved networks in turn: 20 s for a single one, 12 s each when there are several, 35 s in all.
  uint32_t perTry = wOrderN == 1 ? 20000 : 12000, t0 = millis();
  for (uint8_t i = 0; i < wOrderN && millis() - t0 < 35000; i++) {
    wifiBegin(wOrder[i]);
    wTry = (uint8_t)(i + 1);
    uint32_t a = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - a < perTry) delay(200);
    if (WiFi.status() == WL_CONNECTED) return;
  }
  logEvent("wifi: could not join a saved network, starting setup network (still retrying)");
  startAp();    // the router may just be slow: the buddy keeps trying in the background
}

// Called once a second. When there is no WiFi it keeps working through the saved networks (20 s each),
// jumps to a network the person asked for in the web editor, and offers the setup network after 2 minutes
// without any connection (for example after moving the buddy to a new place).
static void wifiRoam(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED && !wSwitchPending) { wLostAt = 0; return; }
  if (gCfg.wifi.n == 0) return;
  if (wLostAt == 0) wLostAt = now;

  if (wSwitchPending) {
    wSwitchPending = false;
    int i = wifiFind(gCfg.wifi, wSwitchTo);
    if (i < 0) return;
    logEvent("wifi: switching to '%s' as asked in the web editor", wSwitchTo);
    WiFi.disconnect(false, false);
    wifiBegin((uint8_t)i);
    wLostAt = now;
    return;
  }

  if (!apActive && now - wLostAt > 120000) {
    logEvent("wifi: no connection for 2 minutes, starting the setup network (long-press the knob for its password)");
    startAp();
  }
  if (apActive && WiFi.softAPgetStationNum() > 0) return;         // someone is using the setup page: do not scan or switch under them
  if (now - wTryAt < 20000) return;                               // give the current attempt time (DHCP can be slow)

  if (wOrderN == 0 || (gCfg.wifi.n > 1 && now - wScanAt > 60000)) wifiPlan(true);
  if (wOrderN == 0) return;
  uint8_t idx = wOrder[wTry % wOrderN];
  if (idx >= gCfg.wifi.n) { wOrderN = 0; return; }               // the list was edited since the plan was made
  wifiBegin(idx);
  wTry++;
}

static void onConnected() {
  if (!mdnsUp && MDNS.begin(gCfg.host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  netApplyTime();
  logEvent("wifi: joined '%s', address %s, signal %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  if (apActive && gCfg.wifi.n > 0) stopAp();
}

static void updateStatus() {
  NetStatus s{};
  bool conn = WiFi.status() == WL_CONNECTED;
  s.connected = conn;
  s.apMode = apActive;
  s.setupNeeded = gCfg.wifi.n == 0;
  s.rssi = conn ? WiFi.RSSI() : 0;
  strlcpy(s.ssid, conn ? WiFi.SSID().c_str() : "", sizeof(s.ssid));
  strlcpy(s.ip, conn ? WiFi.localIP().toString().c_str()
                     : (apActive ? WiFi.softAPIP().toString().c_str() : ""), sizeof(s.ip));
  strlcpy(s.apPin, apPin, sizeof(s.apPin));
  s.timeSynced = time(nullptr) > 1735689600;
  s.bleOn = gCfg.bleHid;
  s.bleConnected = hidConnected();
  s.helperSet = helperConfigured();
  s.helperOk = helperLastOk();
  s.calState = gCfg.calUrl.isEmpty() ? CAL_NOT_SET : calState;
  strlcpy(s.calErr, calErrText, sizeof(s.calErr));
  s.wxState = gCfg.weatherOn ? wxState : CAL_NOT_SET;
  strlcpy(s.wxErr, wxErrText, sizeof(s.wxErr));
  {
    char city[48];
    weatherCityName(gCfg.wPlace.c_str(), city, sizeof city);
    agendaSanitize(city, s.wxPlace, sizeof s.wxPlace);       // the display fonts only have ASCII
  }
  portENTER_CRITICAL(&gMux);
  gNet = s;
  portEXIT_CRITICAL(&gMux);
}

// ------------------------------------------------------------------ calendar polling
static void pollCalendar(uint32_t now) {
  if (gCfg.calUrl.isEmpty()) { calState = CAL_NOT_SET; return; }
  if (WiFi.status() != WL_CONNECTED || time(nullptr) <= 1735689600) return;   // TLS needs a valid clock
  if (!calWanted && (int32_t)(now - calNextAt) < 0) return;

  calWanted = false;
  if (calState == CAL_NOT_SET) calState = CAL_PENDING;
  Agenda a;
  String err;
  static uint32_t errLoggedAt = 0;
  static char     errLogged[96] = "";
  if (calendarFetch(a, err)) {
    portENTER_CRITICAL(&gMux);
    gAgenda = a;
    portEXIT_CRITICAL(&gMux);
    if (calState != CAL_OK) logEvent("calendar: ok, %d upcoming event(s)", (int)a.n);   // later successes stay quiet
    calState = CAL_OK;
    calErrText[0] = 0;
    errLogged[0] = 0;
    calNextAt = now + CAL_EVERY_MS;
  } else {
    // retried every minute: log a new reason at once, the same reason only every 10 minutes
    if (strcmp(errLogged, err.c_str()) != 0 || now - errLoggedAt > 10UL * 60UL * 1000UL) {
      logEvent("calendar: FAILED: %s", err.c_str());
      strlcpy(errLogged, err.c_str(), sizeof errLogged);
      errLoggedAt = now;
    }
    strlcpy(calErrText, err.c_str(), sizeof(calErrText));
    calState = CAL_ERROR;
    calNextAt = now + CAL_RETRY_MS;
  }
}

// ------------------------------------------------------------------ weather polling
static void pollWeather(uint32_t now) {
  if (!gCfg.weatherOn || !weatherCoordsOk(gCfg.wLat, gCfg.wLon)) {
    if (gWeather.valid) { portENTER_CRITICAL(&gMux); gWeather.valid = false; portEXIT_CRITICAL(&gMux); }   // hide it at once
    wxState = CAL_NOT_SET;
    wxErrText[0] = 0;
    return;
  }
  if (WiFi.status() != WL_CONNECTED || time(nullptr) <= 1735689600) return;   // TLS needs a valid clock
  if (!wxWanted && (int32_t)(now - wxNextAt) < 0) return;

  wxWanted = false;
  if (wxState == CAL_NOT_SET) wxState = CAL_PENDING;
  Weather w{};
  String err;
  static uint32_t errLoggedAt = 0;
  static char     errLogged[80] = "";
  if (weatherFetch(w, err)) {
    portENTER_CRITICAL(&gMux);
    gWeather = w;
    portEXIT_CRITICAL(&gMux);
    if (wxState != CAL_OK) logEvent("weather: ok, %d %c, %s", (int)w.temp, w.unit, weatherWords(w.code, w.isDay));   // later successes stay quiet
    wxState = CAL_OK;
    wxErrText[0] = 0;
    errLogged[0] = 0;
    wxNextAt = now + WX_EVERY_MS;
  } else {
    if (strcmp(errLogged, err.c_str()) != 0 || now - errLoggedAt > 10UL * 60UL * 1000UL) {
      logEvent("weather: FAILED: %s", err.c_str());
      strlcpy(errLogged, err.c_str(), sizeof errLogged);
      errLoggedAt = now;
    }
    strlcpy(wxErrText, err.c_str(), sizeof(wxErrText));
    wxState = CAL_ERROR;
    wxNextAt = now + WX_RETRY_MS;
  }
}

// ------------------------------------------------------------------ commands from the UI
static void handleCmd(const NetCmd& c) {
  switch (c.type) {
    case NC_HID:
      if (gCfg.bleHid && !hidRun(c.a)) {
        // the shortcut text itself is never logged: it may contain typed text
        logEvent("shortcut failed: %s", hidConnected() ? "the key sequence is invalid" : "the Mac is not connected over Bluetooth");
        netPostToast("Shortcut failed", true);
      }
      break;
    case NC_ACTION:
      if (!helperAction(c.a)) {
        logEvent("mac helper: action '%s' failed: %s", c.a, helperLastError());
        netPostToast("Mac helper offline", true);
      }
      break;
    case NC_NOTIFY:
      if (!helperNotify(c.a, c.b)) logEvent("mac helper: notification failed: %s", helperLastError());    // best effort
      break;
    case NC_SAVE_TIMER: {
      bool timeOk = time(nullptr) > 1735689600;
      settingsSaveTimer((uint8_t)c.n1, (uint32_t)c.n2, (uint32_t)c.n3, timeOk ? (uint32_t)time(nullptr) : 0);
      break;
    }
    case NC_SAVE_BRIGHT: settingsSave(); break;
    case NC_CAL_REFRESH: calWanted = true; break;
    case NC_WX_REFRESH:  wxWanted = true; break;
  }
}

// ------------------------------------------------------------------ serial diagnostics
// One line every 10 s, so opening the serial monitor at any moment shows where things stand.
static void diagLine() {
  char wifi[96];
  if (apActive)                          snprintf(wifi, sizeof wifi, "setup AP 'CrowPanel-Buddy' pass %s ip %s", apPin, WiFi.softAPIP().toString().c_str());
  else if (WiFi.status() == WL_CONNECTED) snprintf(wifi, sizeof wifi, "joined '%s' ip %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  else                                   snprintf(wifi, sizeof wifi, "not connected");
  Serial.printf("[buddy] up %lus | last reset: %s | wifi: %s | ble: %s (paired Macs: %d) | heap %u psram %u\n",
                (unsigned long)(millis() / 1000), logResetReason(), wifi,
                !gCfg.bleHid ? "off in settings" : hidConnected() ? "connected" : "started, waiting for a Mac",
                hidBondCount(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

// ------------------------------------------------------------------ task
void netTask(void*) {
  photoInit();                          // the Photo clock face's picture, from flash into PSRAM
  wifiBoot();
  webInit();
  if (WiFi.status() == WL_CONNECTED) onConnected();
  if (gCfg.bleHid) {
    hidBegin();
    logEvent("bluetooth: keyboard 'CrowPanel Buddy' started, paired Macs: %d", hidBondCount());
  } else {
    logEvent("bluetooth: switched off in settings");
  }
  logEvent("web editor ready at http://%s.local", gCfg.host.c_str());
  diagLine();

  bool wasConn = WiFi.status() == WL_CONNECTED;
  uint32_t tStatus = 0, tDiag = millis();
  bool wasBle = false, wasTime = false;

  for (;;) {
    webLoop();
    if (apActive) dns.processNextRequest();

    NetCmd c;
    while (xQueueReceive(qToNet, &c, 0) == pdTRUE) handleCmd(c);

    uint32_t now = millis();
    if (now - tStatus > 1000) {
      tStatus = now;
      bool conn = WiFi.status() == WL_CONNECTED;
      if (conn && !wasConn) { onConnected(); calWanted = true; wxWanted = true; }
      if (!conn && wasConn) logEvent("wifi: connection to the router was lost");
      wasConn = conn;
      bool timeNow = time(nullptr) > 1735689600;
      if (timeNow && !wasTime) logEvent("clock: synced from the internet");
      wasTime = timeNow;
      wifiRoam(now);
      pollCalendar(now);
      pollWeather(now);
      updateStatus();
      bool bleNow = hidConnected();
      if (bleNow != wasBle) { wasBle = bleNow; logEvent("bluetooth: Mac %s", bleNow ? "connected" : "disconnected"); }
    }
    if (now - tDiag > 10000) { tDiag = now; diagLine(); }

    if (rebootAt && now > rebootAt) ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
