#include "hid.h"
#include <Arduino.h>
#include "keycombo.h"

#ifndef NO_BLE_HID
#include <BleKeyboard.h>

// keycombo.h uses its own copy of the library's key numbers so it can be tested on a PC.
// These asserts break the build if the library ever disagrees.
static_assert(KC_LCTRL == KEY_LEFT_CTRL && KC_LSHIFT == KEY_LEFT_SHIFT && KC_LALT == KEY_LEFT_ALT && KC_LGUI == KEY_LEFT_GUI, "modifier codes");
static_assert(KC_UP == KEY_UP_ARROW && KC_DOWN == KEY_DOWN_ARROW && KC_LEFT == KEY_LEFT_ARROW && KC_RIGHT == KEY_RIGHT_ARROW, "arrow codes");
static_assert(KC_BACKSPACE == KEY_BACKSPACE && KC_TAB == KEY_TAB && KC_RETURN == KEY_RETURN && KC_ESC == KEY_ESC, "basic key codes");
static_assert(KC_INSERT == KEY_INSERT && KC_DELETE == KEY_DELETE && KC_PGUP == KEY_PAGE_UP && KC_PGDN == KEY_PAGE_DOWN, "edit key codes");
static_assert(KC_HOME == KEY_HOME && KC_END == KEY_END && KC_CAPS == KEY_CAPS_LOCK, "navigation key codes");
static_assert(KC_F1 == KEY_F1 && KC_F1 + 11 == KEY_F12 && KC_F13 == KEY_F13 && KC_F13 + 11 == KEY_F24, "function key codes");

static BleKeyboard* kb = nullptr;

void hidBegin() {
  if (kb) return;
  kb = new BleKeyboard("CrowPanel Buddy", "Elecrow", 100);
  kb->begin();
}

bool hidConnected() { return kb && kb->isConnected(); }

static void sendMedia(uint8_t id) {
  switch (id) {
    case MEDIA_NEXT:       kb->write(KEY_MEDIA_NEXT_TRACK); break;
    case MEDIA_PREV:       kb->write(KEY_MEDIA_PREVIOUS_TRACK); break;
    case MEDIA_STOP:       kb->write(KEY_MEDIA_STOP); break;
    case MEDIA_PLAY_PAUSE: kb->write(KEY_MEDIA_PLAY_PAUSE); break;
    case MEDIA_MUTE:       kb->write(KEY_MEDIA_MUTE); break;
    case MEDIA_VOL_UP:     kb->write(KEY_MEDIA_VOLUME_UP); break;
    case MEDIA_VOL_DOWN:   kb->write(KEY_MEDIA_VOLUME_DOWN); break;
    default: break;
  }
}

bool hidRun(const char* sequence) {
  if (!hidConnected()) return false;
  Step steps[MAX_STEPS];
  int n = parseSequence(sequence, steps, MAX_STEPS);
  if (n == 0) return false;
  for (int i = 0; i < n; i++) {
    const Step& s = steps[i];
    switch (s.kind) {
      case ST_COMBO:
        for (int k = 0; k < s.nkeys; k++) kb->press(s.keys[k]);
        delay(30);                          // hold briefly: macOS ignores zero-length key presses
        kb->releaseAll();
        break;
      case ST_TEXT:
        kb->print(s.text);
        break;
      case ST_WAIT:
        delay(s.ms);
        break;
      case ST_MEDIA:
        sendMedia(s.media);
        break;
    }
    if (s.kind != ST_WAIT && i + 1 < n) delay(40);
  }
  return true;
}

#else  // built with -DNO_BLE_HID

void hidBegin() {}
bool hidConnected() { return false; }
bool hidRun(const char*) { return false; }

#endif
