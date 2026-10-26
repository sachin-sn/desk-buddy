#include "hid.h"
#include <Arduino.h>
#include "keycombo.h"
#include "log.h"

#ifndef NO_BLE_HID
#include <BleKeyboard.h>
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>

// keycombo.h uses its own copy of the library's key numbers so it can be tested on a PC.
// These asserts break the build if the library ever disagrees.
static_assert(KC_LCTRL == KEY_LEFT_CTRL && KC_LSHIFT == KEY_LEFT_SHIFT && KC_LALT == KEY_LEFT_ALT && KC_LGUI == KEY_LEFT_GUI, "modifier codes");
static_assert(KC_UP == KEY_UP_ARROW && KC_DOWN == KEY_DOWN_ARROW && KC_LEFT == KEY_LEFT_ARROW && KC_RIGHT == KEY_RIGHT_ARROW, "arrow codes");
static_assert(KC_BACKSPACE == KEY_BACKSPACE && KC_TAB == KEY_TAB && KC_RETURN == KEY_RETURN && KC_ESC == KEY_ESC, "basic key codes");
static_assert(KC_INSERT == KEY_INSERT && KC_DELETE == KEY_DELETE && KC_PGUP == KEY_PAGE_UP && KC_PGDN == KEY_PAGE_DOWN, "edit key codes");
static_assert(KC_HOME == KEY_HOME && KC_END == KEY_END && KC_CAPS == KEY_CAPS_LOCK, "navigation key codes");
static_assert(KC_F1 == KEY_F1 && KC_F1 + 11 == KEY_F12 && KC_F13 == KEY_F13 && KC_F13 + 11 == KEY_F24, "function key codes");

static BleKeyboard* kb = nullptr;

// Pairing rules. The keyboard library asks for "secure connections + MITM + bonding" and sets nothing else.
// MITM protection needs a way to confirm a code (a screen, a keypad); the buddy has neither, so the pairing
// ends up as an unauthenticated one and a Mac that comes back after a power cycle can be refused for not
// having a key of the strength that was asked for. The result is the symptom "forget the device and pair
// again after every power cycle". These are the settings Espressif's own HID keyboard example uses for a
// device without input or output: plain bonding, "just works", and an encryption key plus an identity key
// handed over in both directions so the Mac recognises the buddy again. Must run after BLEDevice::init
// (inside kb->begin()), which is why it is not part of the constructor.
static void hidSecurity() {
  esp_ble_auth_req_t auth    = ESP_LE_AUTH_BOND;
  esp_ble_io_cap_t   iocap   = ESP_IO_CAP_NONE;
  uint8_t            keySize = 16;
  uint8_t            keys    = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_err_t rc = ESP_OK;
  rc |= esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth,    sizeof auth);
  rc |= esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,      &iocap,   sizeof iocap);
  rc |= esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,    &keySize, sizeof keySize);
  rc |= esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,    &keys,    sizeof keys);
  rc |= esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,     &keys,    sizeof keys);
  logEvent("bluetooth: pairing set to plain bonding, no passcode%s", rc == ESP_OK ? "" : " (some settings were refused)");
}

// Logs how each pairing attempt ended, so the Logs tab shows whether a Mac got paired and kept. Runs on the
// Bluetooth stack's own task: only logs (logEvent is safe from any task), and never prints the Mac's address.
static void hidGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
  if (event != ESP_GAP_BLE_AUTH_CMPL_EVT) return;
  if (p->ble_security.auth_cmpl.success)
    logEvent("bluetooth: a Mac paired, paired Macs now: %d", (int)esp_ble_get_bond_device_num());
  else
    logEvent("bluetooth: pairing failed (reason 0x%02X): forget 'CrowPanel Buddy' on the Mac and pair again",
             (unsigned)p->ble_security.auth_cmpl.fail_reason);
}

void hidBegin() {
  if (kb) return;
  kb = new BleKeyboard("CrowPanel Buddy", "Elecrow", 100);
  kb->begin();
  hidSecurity();
  BLEDevice::setCustomGapHandler(hidGapEvent);
}

bool hidConnected() { return kb && kb->isConnected(); }

int hidBondCount() { return kb ? (int)esp_ble_get_bond_device_num() : -1; }

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
int  hidBondCount() { return -1; }
bool hidRun(const char*) { return false; }

#endif
