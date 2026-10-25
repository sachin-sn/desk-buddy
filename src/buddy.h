#pragma once
#include <Arduino.h>
#include "eyes.h"
#include "lgfx_config.h"

// The buddy screen is drawn straight into a PSRAM sprite and pushed to the panel at ~30 fps.
// While it is showing, LVGL is idle; when another screen takes over, LVGL repaints everything.

struct BuddyView {
  eyes::Mood  mood;
  const char* banner;        // short line under the eyes (ASCII), "" for none
  uint16_t    bannerColor;   // RGB565
  const char* toast;         // brief message in a pill, "" for none
};

bool buddyInit(LGFX& tft);                      // false if the sprite could not be allocated
bool buddyAvailable();
void buddyEnter(uint32_t nowMs);                // call whenever the buddy screen becomes active
void buddyLook(float x, float y, uint32_t nowMs);   // -1..1, e.g. when the knob turns
void buddyPoke(uint32_t nowMs);                 // startled reaction
bool buddyFrame(uint32_t nowMs, const BuddyView& v);   // draws and pushes a frame if one is due
