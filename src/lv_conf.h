// Minimal LVGL 8.3 config. Anything not set here takes lv_conf_internal.h defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0          // LovyanGFX does the byte swap in writePixels

// Allocate LVGL's heap from the normal malloc (goes to PSRAM when large)
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

// Widgets we use: label, arc, line, img, btn (all default on). Turn off the rest.
#define LV_USE_CHART 0
#define LV_USE_TABLE 0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 0
#define LV_USE_TEXTAREA 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_METER 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_SWITCH 0
#define LV_USE_SLIDER 0
#define LV_USE_BAR 0

#endif
