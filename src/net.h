#pragma once
#include <Arduino.h>

// Core 0: WiFi, setup portal, web server, NTP, calendar polling, Mac helper, BLE keyboard, NVS writes.
void netTask(void* arg);

// Helpers shared with web.cpp (core 0 only)
void netPostLayout();                                   // re-read the layout from NVS and hand it to the UI
void netPostToast(const char* text, bool error = false);
void netApplyTime();                                    // (re)configure timezone + NTP
void netRequestReboot();                                // reboot shortly, after the HTTP reply is sent
void netRequestCalendar();                              // fetch the calendar at the next opportunity
bool netApActive();
