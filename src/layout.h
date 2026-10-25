#pragma once
#include <Arduino.h>
#include "state.h"

// The layout is one JSON document edited in the web UI:
//   { "pages":   [ { "name":"Mac", "color":"#00D1FF",
//                    "slots":[ {"label":"Spot","hid":"cmd+space"}, {"label":"Code","mac":"vscode"} ] } ],
//     "presets": [ { "label":"Focus", "min":25 } ] }
// It is stored in NVS (so re-flashing the web UI never wipes it) and parsed into a Layout for the UI.

#define LAYOUT_JSON_MAX 3800      // NVS strings are limited to 4000 bytes

String layoutDefaultJson();
bool   layoutParse(const String& json, Layout& out, String& err);   // validates, incl. shortcut syntax
Layout* layoutLoad();                                               // saved layout, else the default; never null
