#pragma once
#include <Arduino.h>
#include "wifilist.h"

// Core 0: WiFi, setup portal, web server, NTP, calendar polling, Mac helper, BLE keyboard, NVS writes.
void netTask(void* arg);

// Helpers shared with web.cpp (core 0 only)
void netPostLayout();                                   // re-read the layout from NVS and hand it to the UI
void netPostToast(const char* text, bool error = false);
void netPostNote(const char* app, const char* title);      // a Mac notification to show on the buddy (already cleaned)
void netApplyTime();                                    // (re)configure timezone + NTP
void netRequestReboot();                                // reboot shortly, after the HTTP reply is sent
void netRequestCalendar();                              // fetch the calendar at the next opportunity
void netRequestWeather();                               // fetch the weather at the next opportunity
int  netScan(WifiHit* hits, int maxHits);                     // scan (blocks 2-4 s); one entry per name, strongest first; -1 on failure
void netWifiListChanged();                              // the saved networks were edited: re-plan the order to try them in
void netRequestWifiSwitch(const char* ssid);            // leave this network and join the saved one with that name
bool netApActive();
