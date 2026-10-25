#include "settings.h"
#include <Preferences.h>
#include <esp_system.h>

Settings gCfg;
static Preferences prefs;

static String randomToken() {
  char buf[25];
  for (int i = 0; i < 3; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
  return String(buf);
}

// Saved networks live under ws0..ws4 (name) and wp0..wp4 (password). Older firmware kept a single
// network under "ssid"/"pass"; it becomes the first entry. Loading is lenient (no password rules) so a
// network that worked before is never dropped.
static void addWifiRaw(const String& ssid, const String& pass) {
  if (ssid.isEmpty() || ssid.length() > WIFI_SSID_MAX || pass.length() > WIFI_PASS_MAX || gCfg.wifi.n >= WIFI_MAX) return;
  WifiNet& w = gCfg.wifi.net[gCfg.wifi.n++];
  memset(&w, 0, sizeof w);
  strncpy(w.ssid, ssid.c_str(), WIFI_SSID_MAX);
  strncpy(w.pass, pass.c_str(), WIFI_PASS_MAX);
}

static void loadWifi() {      // prefs must be open
  wifiClear(gCfg.wifi);
  if (prefs.isKey("ws0")) {
    for (int i = 0; i < WIFI_MAX; i++) {
      char ks[4] = {'w', 's', (char)('0' + i), 0}, kp[4] = {'w', 'p', (char)('0' + i), 0};
      addWifiRaw(prefs.getString(ks, ""), prefs.getString(kp, ""));
    }
  } else {
    addWifiRaw(prefs.getString("ssid", ""), prefs.getString("pass", ""));
  }
}

void settingsLoad() {
  prefs.begin("buddy", true);
  loadWifi();
  gCfg.adminPass   = prefs.getString("admin", "");
  gCfg.host        = prefs.getString("host", "buddy");
  gCfg.tz          = prefs.getString("tz", "IST-5:30");
  gCfg.calUrl      = prefs.getString("calurl", "");
  gCfg.helperHost  = prefs.getString("helper", "");
  gCfg.helperPort  = prefs.getUShort("hport", 8765);
  gCfg.apiToken    = prefs.getString("token", "");
  gCfg.bright      = prefs.getUChar("bright", 200);
  gCfg.nightBright = prefs.getUChar("nbright", 12);
  gCfg.sleepFrom   = prefs.getUChar("sfrom", 23);
  gCfg.sleepTo     = prefs.getUChar("sto", 7);
  gCfg.leadMin     = prefs.getUChar("lead", 10);
  gCfg.clockSecs   = prefs.getUChar("cto", 15);
  gCfg.ledOn       = prefs.getBool("led", true);
  gCfg.bleHid      = prefs.getBool("ble", true);
  gCfg.weatherOn   = prefs.getBool("wx", false);
  gCfg.wLat        = prefs.getFloat("wlat", 0);
  gCfg.wLon        = prefs.getFloat("wlon", 0);
  gCfg.wPlace      = prefs.getString("wname", "");
  gCfg.wUnit       = prefs.getUChar("wunit", 'C') == 'F' ? 'F' : 'C';
  prefs.end();

  if (gCfg.apiToken.isEmpty()) {          // first boot: make the shared secret once
    gCfg.apiToken = randomToken();
    settingsSave();
  }
}

void settingsSave() {
  prefs.begin("buddy", false);
  for (int i = 0; i < WIFI_MAX; i++) {
    char ks[4] = {'w', 's', (char)('0' + i), 0}, kp[4] = {'w', 'p', (char)('0' + i), 0};
    prefs.putString(ks, i < gCfg.wifi.n ? gCfg.wifi.net[i].ssid : "");
    prefs.putString(kp, i < gCfg.wifi.n ? gCfg.wifi.net[i].pass : "");
  }
  prefs.remove("ssid");              // the single-network keys of older firmware, now migrated
  prefs.remove("pass");
  prefs.putString("admin", gCfg.adminPass);
  prefs.putString("host", gCfg.host);
  prefs.putString("tz", gCfg.tz);
  prefs.putString("calurl", gCfg.calUrl);
  prefs.putString("helper", gCfg.helperHost);
  prefs.putUShort("hport", gCfg.helperPort);
  prefs.putString("token", gCfg.apiToken);
  prefs.putUChar("bright", gCfg.bright);
  prefs.putUChar("nbright", gCfg.nightBright);
  prefs.putUChar("sfrom", gCfg.sleepFrom);
  prefs.putUChar("sto", gCfg.sleepTo);
  prefs.putUChar("lead", gCfg.leadMin);
  prefs.putUChar("cto", gCfg.clockSecs);
  prefs.putBool("led", gCfg.ledOn);
  prefs.putBool("ble", gCfg.bleHid);
  prefs.putBool("wx", gCfg.weatherOn);
  prefs.putFloat("wlat", gCfg.wLat);
  prefs.putFloat("wlon", gCfg.wLon);
  prefs.putString("wname", gCfg.wPlace);
  prefs.putUChar("wunit", (uint8_t)gCfg.wUnit);
  prefs.end();
}

String settingsLoadLayoutJson() {
  prefs.begin("buddy", true);
  String s = prefs.getString("layout", "");
  prefs.end();
  return s;
}

void settingsSaveLayoutJson(const String& json) {
  prefs.begin("buddy", false);
  prefs.putString("layout", json);
  prefs.end();
}

SavedTimer settingsLoadTimer() {
  SavedTimer t{};
  prefs.begin("buddy", true);
  t.state   = prefs.getUChar("t_st", 0);
  t.dur     = prefs.getUInt("t_dur", 25 * 60);
  t.rem     = prefs.getUInt("t_rem", 0);
  t.savedAt = prefs.getUInt("t_at", 0);
  prefs.end();
  return t;
}

void settingsSaveTimer(uint8_t state, uint32_t dur, uint32_t rem, uint32_t nowEpoch) {
  prefs.begin("buddy", false);
  prefs.putUChar("t_st", state);
  prefs.putUInt("t_dur", dur);
  prefs.putUInt("t_rem", rem);
  prefs.putUInt("t_at", nowEpoch);
  prefs.end();
}
