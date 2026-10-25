#include "net.h"
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
#include "web.h"

static DNSServer dns;
static bool     apActive = false;
static uint32_t rebootAt = 0;
static bool     mdnsUp = false;
static char     apPin[12] = "";

static bool     calWanted = true;            // fetch as soon as WiFi and time are ready
static uint32_t calNextAt = 0;               // millis() of the next scheduled fetch
static uint8_t  calState = CAL_NOT_SET;
static char     calErrText[96] = "";
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

void netApplyTime() {
  configTzTime(gCfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void netRequestReboot()   { rebootAt = millis() + 800; logf("restarting in a moment"); }
void netRequestCalendar() { calWanted = true; }
bool netApActive()        { return apActive; }

// ------------------------------------------------------------------ WiFi
static void startAp() {
  if (apActive) return;
  snprintf(apPin, sizeof(apPin), "%08u", (unsigned)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP_STA);                  // keep trying the saved network in the background
  WiFi.softAP("CrowPanel-Buddy", apPin);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  logf("wifi: setup network 'CrowPanel-Buddy' is up (password is on the buddy's screen)");
}

static void stopAp() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  logf("wifi: setup network stopped");
}

static const char* wifiReasonText(uint8_t r) {
  switch (r) {
    case 2:   return "authentication expired";
    case 8:   return "router dropped us";
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
    logf("wifi: not connected, reason %u (%s)", (unsigned)r, wifiReasonText(r));
  }
}

static void wifiBoot() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(gCfg.host.c_str());
  WiFi.onEvent(onWifiEvent);
  if (gCfg.ssid.isEmpty()) { logf("wifi: no network saved yet, starting setup"); startAp(); return; }
  logf("wifi: joining '%s'", gCfg.ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(gCfg.ssid.c_str(), gCfg.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
  if (WiFi.status() != WL_CONNECTED) {
    logf("wifi: could not join '%s' within 20 s, starting setup network (still retrying)", gCfg.ssid.c_str());
    startAp();    // router may just be slow: STA keeps retrying
  }
}

static void onConnected() {
  if (!mdnsUp && MDNS.begin(gCfg.host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
  netApplyTime();
  logf("wifi: joined '%s', address %s, signal %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  if (apActive && !gCfg.ssid.isEmpty()) stopAp();
}

static void updateStatus() {
  NetStatus s{};
  bool conn = WiFi.status() == WL_CONNECTED;
  s.connected = conn;
  s.apMode = apActive;
  s.setupNeeded = gCfg.ssid.isEmpty();
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
    if (calState != CAL_OK) logf("calendar: ok, %d upcoming event(s)", (int)a.n);   // later successes stay quiet
    calState = CAL_OK;
    calErrText[0] = 0;
    errLogged[0] = 0;
    calNextAt = now + CAL_EVERY_MS;
  } else {
    // retried every minute: log a new reason at once, the same reason only every 10 minutes
    if (strcmp(errLogged, err.c_str()) != 0 || now - errLoggedAt > 10UL * 60UL * 1000UL) {
      logf("calendar: FAILED: %s", err.c_str());
      strlcpy(errLogged, err.c_str(), sizeof errLogged);
      errLoggedAt = now;
    }
    strlcpy(calErrText, err.c_str(), sizeof(calErrText));
    calState = CAL_ERROR;
    calNextAt = now + CAL_RETRY_MS;
  }
}

// ------------------------------------------------------------------ commands from the UI
static void handleCmd(const NetCmd& c) {
  switch (c.type) {
    case NC_HID:
      if (gCfg.bleHid && !hidRun(c.a)) {
        // the shortcut text itself is never logged: it may contain typed text
        logf("shortcut failed: %s", hidConnected() ? "the key sequence is invalid" : "the Mac is not connected over Bluetooth");
        netPostToast("Shortcut failed", true);
      }
      break;
    case NC_ACTION:
      if (!helperAction(c.a)) {
        logf("mac helper: action '%s' failed: %s", c.a, helperLastError());
        netPostToast("Mac helper offline", true);
      }
      break;
    case NC_NOTIFY:
      if (!helperNotify(c.a, c.b)) logf("mac helper: notification failed: %s", helperLastError());    // best effort
      break;
    case NC_SAVE_TIMER: {
      bool timeOk = time(nullptr) > 1735689600;
      settingsSaveTimer((uint8_t)c.n1, (uint32_t)c.n2, (uint32_t)c.n3, timeOk ? (uint32_t)time(nullptr) : 0);
      break;
    }
    case NC_SAVE_BRIGHT: settingsSave(); break;
    case NC_CAL_REFRESH: calWanted = true; break;
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
  wifiBoot();
  webInit();
  if (WiFi.status() == WL_CONNECTED) onConnected();
  if (gCfg.bleHid) {
    hidBegin();
    logf("bluetooth: keyboard 'CrowPanel Buddy' started, paired Macs: %d", hidBondCount());
  } else {
    logf("bluetooth: switched off in settings");
  }
  logf("web editor ready at http://%s.local", gCfg.host.c_str());
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
      if (conn && !wasConn) { onConnected(); calWanted = true; }
      if (!conn && wasConn) logf("wifi: connection to the router was lost");
      wasConn = conn;
      bool timeNow = time(nullptr) > 1735689600;
      if (timeNow && !wasTime) logf("clock: synced from the internet");
      wasTime = timeNow;
      pollCalendar(now);
      updateStatus();
      bool bleNow = hidConnected();
      if (bleNow != wasBle) { wasBle = bleNow; logf("bluetooth: Mac %s", bleNow ? "connected" : "disconnected"); }
    }
    if (now - tDiag > 10000) { tDiag = now; diagLine(); }

    if (rebootAt && now > rebootAt) ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
