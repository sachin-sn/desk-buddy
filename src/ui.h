#pragma once
#include "lgfx_config.h"

extern LGFX tft;

void uiInit();                 // display + LVGL + input + LED ring; call once from setup(), before the tasks start
void uiTask(void* arg);        // pinned to core 1; never blocks on files, NVS, WiFi or BLE
