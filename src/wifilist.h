#pragma once
// The WiFi networks the buddy knows (home, office, phone hotspot, ...). Pure (no Arduino), so the
// rules are unit-tested on a PC. The list order is the priority order: the first saved network that
// is in range is the one the buddy joins.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WIFI_MAX       5
#define WIFI_SSID_MAX  32
#define WIFI_PASS_MAX  63

struct WifiNet {
  char ssid[WIFI_SSID_MAX + 1];
  char pass[WIFI_PASS_MAX + 1];      // empty = open network
};

struct WifiList {
  WifiNet net[WIFI_MAX];
  uint8_t n;
};

enum WifiResult : uint8_t { WIFI_OK = 0, WIFI_BAD_SSID, WIFI_BAD_PASS, WIFI_FULL, WIFI_NOT_FOUND };

inline const char* wifiResultText(WifiResult r) {
  switch (r) {
    case WIFI_OK:        return "ok";
    case WIFI_BAD_SSID:  return "network name must be 1 to 32 characters";
    case WIFI_BAD_PASS:  return "WiFi password must be 8 to 63 characters (or empty for an open network)";
    case WIFI_FULL:      return "the buddy remembers up to 5 networks: remove one first";
    case WIFI_NOT_FOUND: return "that network is not in the list";
  }
  return "error";
}

inline bool wifiHasControlChars(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) if (*p < 0x20 || *p == 0x7F) return true;
  return false;
}

inline bool wifiSsidOk(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  return n >= 1 && n <= WIFI_SSID_MAX && !wifiHasControlChars(s);
}

// WPA2 passphrases are 8..63 characters. Empty means an open network.
inline bool wifiPassOk(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  return (n == 0 || (n >= 8 && n <= WIFI_PASS_MAX)) && !wifiHasControlChars(s);
}

inline int wifiFind(const WifiList& l, const char* ssid) {
  if (!ssid) return -1;
  for (int i = 0; i < l.n; i++) if (strcmp(l.net[i].ssid, ssid) == 0) return i;      // SSIDs are case-sensitive
  return -1;
}

inline void wifiClear(WifiList& l) { memset(&l, 0, sizeof l); }

// Add a network at the end (lowest priority), or update the one with the same name in place.
// pass == nullptr keeps the saved password of an existing network (an empty one for a new network).
// toFront moves the network to the top of the list.
inline WifiResult wifiSet(WifiList& l, const char* ssid, const char* pass, bool toFront) {
  if (!wifiSsidOk(ssid)) return WIFI_BAD_SSID;
  if (pass && !wifiPassOk(pass)) return WIFI_BAD_PASS;
  int i = wifiFind(l, ssid);
  if (i < 0) {
    if (l.n >= WIFI_MAX) return WIFI_FULL;
    i = l.n++;
    memset(&l.net[i], 0, sizeof l.net[i]);
    strncpy(l.net[i].ssid, ssid, WIFI_SSID_MAX);
  }
  if (pass) { memset(l.net[i].pass, 0, sizeof l.net[i].pass); strncpy(l.net[i].pass, pass, WIFI_PASS_MAX); }
  if (toFront && i > 0) {
    WifiNet keep = l.net[i];
    for (int j = i; j > 0; j--) l.net[j] = l.net[j - 1];
    l.net[0] = keep;
  }
  return WIFI_OK;
}

inline WifiResult wifiRemove(WifiList& l, const char* ssid) {
  int i = wifiFind(l, ssid);
  if (i < 0) return WIFI_NOT_FOUND;
  for (int j = i; j + 1 < l.n; j++) l.net[j] = l.net[j + 1];
  l.n--;
  memset(&l.net[l.n], 0, sizeof l.net[l.n]);          // do not leave a removed password behind
  return WIFI_OK;
}

// dir < 0: higher priority (towards the top); dir > 0: lower. At the edge nothing changes.
inline WifiResult wifiMove(WifiList& l, const char* ssid, int dir) {
  int i = wifiFind(l, ssid);
  if (i < 0) return WIFI_NOT_FOUND;
  int j = i + (dir < 0 ? -1 : dir > 0 ? 1 : 0);
  if (j < 0 || j >= l.n || j == i) return WIFI_OK;
  WifiNet t = l.net[i]; l.net[i] = l.net[j]; l.net[j] = t;
  return WIFI_OK;
}

struct WifiHit {                    // one access point seen by a scan
  char   ssid[WIFI_SSID_MAX + 1];
  int8_t rssi;
  bool   open;                      // no password needed
};

// A scan lists every access point, so a mesh shows the same name several times. Keep one entry per
// name (the strongest) and sort the strongest first. Returns the new count.
inline int wifiTidyHits(WifiHit* h, int n) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!h[i].ssid[0]) continue;                                  // hidden network: no name to show
    int j = 0;
    while (j < k && strcmp(h[j].ssid, h[i].ssid) != 0) j++;
    if (j < k) { if (h[i].rssi > h[j].rssi) { h[j].rssi = h[i].rssi; h[j].open = h[i].open; } continue; }
    if (k != i) h[k] = h[i];
    k++;
  }
  for (int i = 1; i < k; i++) {                                   // insertion sort, strongest first
    WifiHit t = h[i];
    int j = i - 1;
    while (j >= 0 && h[j].rssi < t.rssi) { h[j + 1] = h[j]; j--; }
    h[j + 1] = t;
  }
  return k;
}

// The order to try the saved networks in: those seen in the scan first (list order, so the list order
// is the priority), then the rest (a hidden network does not show up in scans). Returns how many.
inline int wifiOrder(const WifiList& l, const WifiHit* hits, int nHits, uint8_t* out) {
  int k = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < l.n; i++) {
      bool seen = false;
      for (int h = 0; h < nHits && hits; h++) if (strcmp(hits[h].ssid, l.net[i].ssid) == 0) { seen = true; break; }
      if (seen == (pass == 0)) out[k++] = (uint8_t)i;
    }
  }
  return k;
}
