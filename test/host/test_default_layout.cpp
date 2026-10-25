// Checks the built-in default layout (src/layout.cpp) without needing the Arduino JSON library:
// the raw JSON string is cut out of the source and scanned. Every shortcut must parse with the same
// parser the firmware uses, and the layout must respect the limits the firmware enforces.
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "check.h"
#include "keycombo.h"

static std::string slurp(const char* path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// all values of "key":"value" inside text
static std::vector<std::string> values(const std::string& text, const std::string& key) {
  std::vector<std::string> out;
  std::string needle = "\"" + key + "\":\"";
  size_t p = 0;
  while ((p = text.find(needle, p)) != std::string::npos) {
    p += needle.size();
    size_t e = text.find('"', p);
    out.push_back(text.substr(p, e - p));
    p = e;
  }
  return out;
}

int main() {
  std::string src = slurp("../../src/layout.cpp");
  CHECK(!src.empty());
  size_t a = src.find("R\"JSON(");
  size_t b = src.find(")JSON\"");
  CHECK(a != std::string::npos && b != std::string::npos && b > a);
  std::string raw = src.substr(a + 7, b - a - 7), json;
  for (char c : raw) if (c != '\n') json += c;             // layoutDefaultJson() strips newlines too

  CHECK(json.size() <= 3800);
  printf("  default layout is %zu characters (limit 3800)\n", json.size());

  // split into pages: each starts with {"name":"..."
  std::vector<size_t> starts;
  size_t p = 0;
  while ((p = json.find("{\"name\":\"", p)) != std::string::npos) { starts.push_back(p); p += 8; }
  size_t presetsAt = json.find("\"presets\":");
  CHECK(presetsAt != std::string::npos);
  CHECK(starts.size() >= 1 && starts.size() <= 6);
  printf("  %zu pages\n", starts.size());

  int slotsTotal = 0;
  for (size_t i = 0; i < starts.size(); i++) {
    size_t end = (i + 1 < starts.size()) ? starts[i + 1] : presetsAt;
    std::string page = json.substr(starts[i], end - starts[i]);
    std::vector<std::string> names = values(page, "name");
    CHECK(!names.empty() && names[0].size() >= 1 && names[0].size() <= 11);
    std::vector<std::string> labels = values(page, "label");
    std::vector<std::string> hids = values(page, "hid");
    std::vector<std::string> macs = values(page, "mac");
    CHECK(labels.size() >= 1 && labels.size() <= 6);
    CHECK(labels.size() == hids.size() + macs.size());         // every slot has exactly one action
    slotsTotal += (int)labels.size();
    for (const std::string& l : labels) CHECK(l.size() >= 1 && l.size() <= 6);
    for (const std::string& h : hids) {
      Step steps[MAX_STEPS];
      char err[64] = "";
      int n = parseSequence(h.c_str(), steps, MAX_STEPS, err, sizeof err);
      if (n <= 0) printf("  BAD SHORTCUT '%s': %s\n", h.c_str(), err);
      CHECK(n > 0);
      CHECK(h.size() < 64);
      CHECK(sequenceWaitMs(steps, n) <= 1500);
    }
    for (const std::string& m : macs) {
      CHECK(m.size() >= 1 && m.size() <= 23);
      for (char c : m) CHECK(isalnum((unsigned char)c) || c == '_' || c == '-');
    }
  }
  printf("  %d slots, all shortcuts parse\n", slotsTotal);

  // every "Mac action" used by the default layout must exist in the helper's example actions.json
  std::string actions = slurp("../../helper/actions.example.json");
  CHECK(!actions.empty());
  std::vector<std::string> macs = values(json, "mac");
  CHECK(!macs.empty());
  for (const std::string& m : macs) {
    bool found = actions.find("\"" + m + "\"") != std::string::npos;
    if (!found) printf("  action id '%s' missing from helper/actions.example.json\n", m.c_str());
    CHECK(found);
  }
  printf("  %zu Mac action ids found in helper/actions.example.json\n", macs.size());

  // timer presets
  std::string presets = json.substr(presetsAt);
  std::vector<std::string> pl = values(presets, "label");
  CHECK(pl.size() >= 1 && pl.size() <= 6);
  for (const std::string& l : pl) CHECK(l.size() >= 1 && l.size() <= 9);
  CHECK(presets.find("\"min\":") != std::string::npos);

  return finish();
}
