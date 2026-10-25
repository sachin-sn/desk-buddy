#include "layout.h"
#include <ArduinoJson.h>
#include "keycombo.h"
#include "settings.h"

// macOS defaults. Keep every "hid" string valid for keycombo.h (test/host/test_keycombo.cpp checks the syntax).
static const char DEFAULT_LAYOUT[] PROGMEM = R"JSON({"pages":[
{"name":"Mac","color":"#00D1FF","slots":[
 {"label":"Spot","hid":"cmd+space"},{"label":"Shot","hid":"cmd+shift+4"},{"label":"Rec","hid":"cmd+shift+5"},
 {"label":"Lock","hid":"ctrl+cmd+q"},{"label":"Apps","hid":"cmd+tab"},{"label":"Desks","hid":"ctrl+up"}]},
{"name":"Calls","color":"#4CD68A","slots":[
 {"label":"Mic","hid":"cmd+d"},{"label":"Cam","hid":"cmd+e"},
 {"label":"Z Mic","hid":"cmd+shift+a"},{"label":"Z Cam","hid":"cmd+shift+v"},
 {"label":"T Mic","hid":"cmd+shift+m"},{"label":"T Cam","hid":"cmd+shift+o"}]},
{"name":"Dev","color":"#B388FF","slots":[
 {"label":"Code","mac":"vscode"},{"label":"Term","mac":"terminal"},{"label":"Web","mac":"chrome"},
 {"label":"Palet","hid":"cmd+shift+p"},{"label":"Tab+","hid":"cmd+t"},{"label":"Undo","hid":"cmd+shift+t"}]},
{"name":"Media","color":"#FF4FD8","slots":[
 {"label":"Play","hid":"playpause"},{"label":"Next","hid":"next"},{"label":"Prev","hid":"prev"},
 {"label":"Vol+","hid":"volup"},{"label":"Vol-","hid":"voldown"},{"label":"Mute","hid":"mute"}]}
],"presets":[
 {"label":"Focus","min":25},{"label":"Break","min":5},{"label":"Long","min":15},
 {"label":"Tea","min":3},{"label":"Quick","min":10},{"label":"Hour","min":60}]})JSON";

String layoutDefaultJson() {
  String s;
  s.reserve(sizeof(DEFAULT_LAYOUT));
  s = FPSTR(DEFAULT_LAYOUT);
  // compact: the stored copy should stay well under the NVS limit
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) if (s[i] != '\n') out += s[i];
  return out;
}

static uint32_t parseColor(const char* s, uint32_t def) {
  if (!s || !*s) return def;
  if (*s == '#') s++;
  if (strlen(s) != 6) return def;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  return (end && *end == 0) ? (uint32_t)v : def;
}

static bool macIdOk(const char* s) {
  size_t n = strlen(s);
  if (n < 1 || n > 23) return false;
  for (size_t i = 0; i < n; i++) if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-')) return false;
  return true;
}

bool layoutParse(const String& json, Layout& out, String& err) {
  memset(&out, 0, sizeof(out));
  if (json.length() > LAYOUT_JSON_MAX) { err = "layout too large (max 3800 characters)"; return false; }
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, json);
  if (je) { err = String("invalid JSON: ") + je.c_str(); return false; }
  if (!doc["pages"].is<JsonArray>()) { err = "missing \"pages\" array"; return false; }

  int pi = 0;
  for (JsonObject p : doc["pages"].as<JsonArray>()) {
    if (pi >= MAX_PAGES) { err = "at most 6 pages"; return false; }
    Page& pg = out.page[pi];
    const char* name = p["name"] | "";
    if (!*name || strlen(name) > 11) { err = String("page ") + (pi + 1) + ": name needs 1-11 characters"; return false; }
    strlcpy(pg.name, name, sizeof(pg.name));
    pg.color = parseColor(p["color"] | "", 0x00D1FF);

    int si = 0;
    for (JsonObject s : p["slots"].as<JsonArray>()) {
      String where = String("page \"") + name + "\" slot " + (si + 1) + ": ";
      if (si >= MAX_SLOTS) { err = String("page \"") + name + "\": at most 6 slots"; return false; }
      Slot& sl = pg.slot[si];
      const char* label = s["label"] | "";
      const char* hid = s["hid"] | "";
      const char* mac = s["mac"] | "";
      if (!*label || strlen(label) > 6) { err = where + "label needs 1-6 characters"; return false; }
      if (*hid && *mac) { err = where + "use either hid or mac, not both"; return false; }
      if (!*hid && !*mac) { err = where + "needs a hid shortcut or a mac action"; return false; }
      strlcpy(sl.label, label, sizeof(sl.label));
      if (*hid) {
        if (strlen(hid) >= sizeof(sl.hid)) { err = where + "shortcut too long"; return false; }
        Step steps[MAX_STEPS];
        char e[48];
        int n = parseSequence(hid, steps, MAX_STEPS, e, sizeof(e));
        if (n == 0) { err = where + e; return false; }
        if (sequenceWaitMs(steps, n) > 1500) { err = where + "waits add up to more than 1.5 s"; return false; }
        strlcpy(sl.hid, hid, sizeof(sl.hid));
      } else {
        if (!macIdOk(mac)) { err = where + "mac action id: letters, digits, - and _ (max 23)"; return false; }
        strlcpy(sl.mac, mac, sizeof(sl.mac));
      }
      si++;
    }
    pg.n = si;
    pi++;
  }
  out.pageCount = pi;
  if (pi == 0) { err = "add at least one page"; return false; }

  int ti = 0;
  for (JsonObject t : doc["presets"].as<JsonArray>()) {
    if (ti >= MAX_PRESETS) { err = "at most 6 timer presets"; return false; }
    const char* label = t["label"] | "";
    float mins = t["min"] | 0.0f;
    if (!*label || strlen(label) > 9) { err = String("preset ") + (ti + 1) + ": label needs 1-9 characters"; return false; }
    if (mins < 1.0f || mins > 5999.0f) { err = String("preset \"") + label + "\": minutes must be 1 to 5999"; return false; }
    Preset& pr = out.preset[ti++];
    strlcpy(pr.label, label, sizeof(pr.label));
    pr.secs = (uint32_t)(mins * 60.0f + 0.5f);
  }
  out.presetCount = ti;
  if (ti == 0) {                       // always have at least one preset to start from
    strlcpy(out.preset[0].label, "Timer", sizeof(out.preset[0].label));
    out.preset[0].secs = 5 * 60;
    out.presetCount = 1;
  }
  return true;
}

Layout* layoutLoad() {
  Layout* lay = new Layout();
  String err;
  String saved = settingsLoadLayoutJson();
  if (!saved.isEmpty() && layoutParse(saved, *lay, err)) return lay;
  layoutParse(layoutDefaultJson(), *lay, err);       // the built-in default is valid by construction
  return lay;
}
