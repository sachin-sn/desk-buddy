// Saved WiFi networks: validation, add/update/remove/reorder, and the order they are tried in.
#include <string>
#include "check.h"
#include "wifilist.h"

static std::string names(const WifiList& l) {
  std::string s;
  for (int i = 0; i < l.n; i++) { if (i) s += ","; s += l.net[i].ssid; }
  return s;
}

int main() {
  // validation
  CHECK(wifiSsidOk("Home"));
  CHECK(wifiSsidOk("a"));
  CHECK(wifiSsidOk("12345678901234567890123456789012"));        // 32
  CHECK(!wifiSsidOk("123456789012345678901234567890123"));      // 33
  CHECK(!wifiSsidOk(""));
  CHECK(!wifiSsidOk(nullptr));
  CHECK(!wifiSsidOk("bad\nname"));
  CHECK(wifiSsidOk("Caf\xC3\xA9 Wi-Fi"));                       // UTF-8 is fine
  CHECK(wifiPassOk(""));                                        // open network
  CHECK(wifiPassOk("12345678"));
  CHECK(!wifiPassOk("1234567"));                                // too short for WPA2
  CHECK(wifiPassOk("123456789012345678901234567890123456789012345678901234567890123"));   // 63
  CHECK(!wifiPassOk("1234567890123456789012345678901234567890123456789012345678901234")); // 64
  CHECK(!wifiPassOk("pass\tword1"));
  CHECK(!wifiPassOk(nullptr));

  // add, update, full
  WifiList l;
  wifiClear(l);
  CHECK_EQ(l.n, 0);
  CHECK_EQ(wifiSet(l, "Home", "homepass1", false), WIFI_OK);
  CHECK_EQ(wifiSet(l, "Office", "officepass", false), WIFI_OK);
  CHECK_EQ(wifiSet(l, "Phone", "hotspot123", false), WIFI_OK);
  CHECK_STR(names(l).c_str(), "Home,Office,Phone");
  CHECK_STR(l.net[1].pass, "officepass");
  CHECK_EQ(wifiSet(l, "Office", "newpass123", false), WIFI_OK);      // same name: password replaced in place
  CHECK_EQ(l.n, 3);
  CHECK_STR(l.net[1].pass, "newpass123");
  CHECK_EQ(wifiSet(l, "Office", nullptr, false), WIFI_OK);           // nullptr keeps the password
  CHECK_STR(l.net[1].pass, "newpass123");
  CHECK_EQ(wifiSet(l, "Office", "", false), WIFI_OK);                // empty sets an open network
  CHECK_STR(l.net[1].pass, "");
  CHECK_EQ(wifiSet(l, "Cafe", nullptr, false), WIFI_OK);             // new network without a password is open
  CHECK_STR(l.net[3].pass, "");
  CHECK_EQ(wifiSet(l, "Gym", "gympass99", false), WIFI_OK);
  CHECK_EQ(l.n, WIFI_MAX);
  CHECK_EQ(wifiSet(l, "Sixth", "sixthpass1", false), WIFI_FULL);
  CHECK_EQ(wifiSet(l, "Gym", "gympass00", false), WIFI_OK);          // updating still works when full
  CHECK_EQ(l.n, WIFI_MAX);
  CHECK_EQ(wifiSet(l, "", "whatever12", false), WIFI_BAD_SSID);
  CHECK_EQ(wifiSet(l, "Bad", "short", false), WIFI_BAD_PASS);
  CHECK_EQ(l.n, WIFI_MAX);                                           // rejected input changes nothing
  CHECK_EQ(wifiFind(l, "home"), -1);                                 // case-sensitive
  CHECK_EQ(wifiFind(l, "Home"), 0);
  CHECK_EQ(wifiFind(l, nullptr), -1);

  // toFront
  CHECK_EQ(wifiSet(l, "Gym", nullptr, true), WIFI_OK);
  CHECK_STR(names(l).c_str(), "Gym,Home,Office,Phone,Cafe");
  CHECK_STR(l.net[0].pass, "gympass00");                             // password moved with it
  CHECK_EQ(wifiSet(l, "Gym", nullptr, true), WIFI_OK);               // already first: no change
  CHECK_STR(names(l).c_str(), "Gym,Home,Office,Phone,Cafe");

  // move
  CHECK_EQ(wifiMove(l, "Phone", -1), WIFI_OK);
  CHECK_STR(names(l).c_str(), "Gym,Home,Phone,Office,Cafe");
  CHECK_EQ(wifiMove(l, "Gym", -1), WIFI_OK);                         // already on top
  CHECK_STR(names(l).c_str(), "Gym,Home,Phone,Office,Cafe");
  CHECK_EQ(wifiMove(l, "Cafe", +1), WIFI_OK);                        // already last
  CHECK_STR(names(l).c_str(), "Gym,Home,Phone,Office,Cafe");
  CHECK_EQ(wifiMove(l, "Gym", +1), WIFI_OK);
  CHECK_STR(names(l).c_str(), "Home,Gym,Phone,Office,Cafe");
  CHECK_EQ(wifiMove(l, "Nope", +1), WIFI_NOT_FOUND);

  // remove
  CHECK_EQ(wifiRemove(l, "Gym"), WIFI_OK);
  CHECK_STR(names(l).c_str(), "Home,Phone,Office,Cafe");
  CHECK_EQ(l.n, 4);
  CHECK_EQ(l.net[4].ssid[0], 0);                                     // the vacated slot is wiped
  CHECK_EQ(l.net[4].pass[0], 0);
  CHECK_EQ(wifiRemove(l, "Gym"), WIFI_NOT_FOUND);
  CHECK_EQ(wifiRemove(l, "Home"), WIFI_OK);
  CHECK_EQ(wifiRemove(l, "Phone"), WIFI_OK);
  CHECK_EQ(wifiRemove(l, "Office"), WIFI_OK);
  CHECK_EQ(wifiRemove(l, "Cafe"), WIFI_OK);
  CHECK_EQ(l.n, 0);
  CHECK_EQ(wifiRemove(l, "Cafe"), WIFI_NOT_FOUND);

  // order to try: seen networks first, in list (priority) order, then the others
  wifiClear(l);
  wifiSet(l, "Home", "homepass1", false);
  wifiSet(l, "Office", "officepass", false);
  wifiSet(l, "Phone", "hotspot123", false);
  wifiSet(l, "Hidden", "hiddenpass1", false);
  WifiHit hits[3];
  memset(hits, 0, sizeof hits);
  strcpy(hits[0].ssid, "Phone");  hits[0].rssi = -40;
  strcpy(hits[1].ssid, "Office"); hits[1].rssi = -70;
  strcpy(hits[2].ssid, "Stranger"); hits[2].rssi = -50;
  uint8_t order[WIFI_MAX];
  int k = wifiOrder(l, hits, 3, order);
  CHECK_EQ(k, 4);
  CHECK_EQ(order[0], 1);     // Office (higher in the list than Phone, even though Phone is stronger)
  CHECK_EQ(order[1], 2);     // Phone
  CHECK_EQ(order[2], 0);     // Home: saved but not seen
  CHECK_EQ(order[3], 3);     // Hidden
  k = wifiOrder(l, nullptr, 0, order);                // nothing scanned: plain list order
  CHECK_EQ(k, 4);
  CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
  WifiList empty;
  wifiClear(empty);
  CHECK_EQ(wifiOrder(empty, hits, 3, order), 0);

  // scan results: one entry per name, strongest first, hidden (nameless) ones dropped
  WifiHit sc[7];
  memset(sc, 0, sizeof sc);
  strcpy(sc[0].ssid, "Mesh");   sc[0].rssi = -80; sc[0].open = false;
  strcpy(sc[1].ssid, "Cafe");   sc[1].rssi = -60; sc[1].open = true;
  strcpy(sc[2].ssid, "Mesh");   sc[2].rssi = -50; sc[2].open = false;      // same name, stronger
  sc[3].rssi = -30;                                                         // hidden
  strcpy(sc[4].ssid, "Phone");  sc[4].rssi = -70; sc[4].open = false;
  strcpy(sc[5].ssid, "Cafe");   sc[5].rssi = -90; sc[5].open = true;       // same name, weaker
  strcpy(sc[6].ssid, "Far");    sc[6].rssi = -95;
  int m = wifiTidyHits(sc, 7);
  CHECK_EQ(m, 4);
  CHECK_STR(sc[0].ssid, "Mesh");  CHECK_EQ(sc[0].rssi, -50);
  CHECK_STR(sc[1].ssid, "Cafe");  CHECK_EQ(sc[1].rssi, -60); CHECK(sc[1].open);
  CHECK_STR(sc[2].ssid, "Phone");
  CHECK_STR(sc[3].ssid, "Far");
  CHECK_EQ(wifiTidyHits(sc, 0), 0);

  CHECK(strlen(wifiResultText(WIFI_FULL)) > 5);
  return finish();
}
