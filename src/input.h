#pragma once
#include <Arduino.h>

// Encoder detents: how many quadrature counts make one click on your knob.
// Most EC11 knobs are 4 (or 2). If one click moves 2 steps, change to 2.
#define ENC_COUNTS_PER_DETENT 4
#define ENC_REVERSE 0            // set 1 if clockwise reads as counter-clockwise

#define BTN_LONG_MS   800
#define TOUCH_LONG_MS 700
#define SWIPE_MIN_PX  40

enum InputType : uint8_t {
  IN_ENC_CW, IN_ENC_CCW, IN_BTN_SHORT, IN_BTN_LONG,
  IN_SWIPE_L, IN_SWIPE_R, IN_SWIPE_U, IN_SWIPE_D,
  IN_TAP, IN_LONG_TOUCH
};
struct InputEvent {
  InputType type;
  int16_t x, y;   // touch position for TAP / LONG_TOUCH, 0 otherwise
};

void inputInit();
bool inputPoll(InputEvent& ev);   // returns true while events are pending
