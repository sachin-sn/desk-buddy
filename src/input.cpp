#include "input.h"
#include "pins.h"
#include <Wire.h>

// ------------------------------------------------------------------ encoder
static volatile int32_t encCount = 0;
static volatile uint8_t encState = 0;

static void IRAM_ATTR encISR() {
  static const int8_t tbl[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encState = ((encState << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  encCount += tbl[encState];
}

// ------------------------------------------------------------------ small event FIFO
static InputEvent fifo[16];
static uint8_t fHead = 0, fTail = 0;
static void push(InputType t, int16_t x = 0, int16_t y = 0) {
  uint8_t n = (fHead + 1) & 15;
  if (n == fTail) return;          // full: drop
  fifo[fHead] = {t, x, y};
  fHead = n;
}

// ------------------------------------------------------------------ CST816D touch
#define TOUCH_ADDR 0x15
static uint8_t touchFails = 0;

static void touchReset() {
  digitalWrite(PIN_TOUCH_RST, LOW);  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH); delay(60);
  Wire.beginTransmission(TOUCH_ADDR);   // 0xFE = 1 disables auto-sleep so polling keeps working
  Wire.write(0xFE); Wire.write(0x01);
  Wire.endTransmission();
}

static bool touchRead(bool& down, int16_t& x, int16_t& y) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)5) != 5) return false;
  uint8_t fingers = Wire.read();
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  down = (fingers & 0x0F) > 0;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

static bool     tDown = false;
static int16_t  tX0, tY0, tX, tY;
static uint32_t tT0 = 0;
static bool     tLongSent = false;

static void touchPoll() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 15) return;
  last = now;

  bool down = false; int16_t x = 0, y = 0;
  if (!touchRead(down, x, y)) {
    if (++touchFails > 60) { touchFails = 0; touchReset(); }   // chip asleep/hung: reset it
    return;
  }
  touchFails = 0;

  if (down && !tDown) {                       // finger just landed
    tDown = true; tX0 = tX = x; tY0 = tY = y; tT0 = now; tLongSent = false;
  } else if (down && tDown) {                 // dragging
    tX = x; tY = y;
    if (!tLongSent && now - tT0 > TOUCH_LONG_MS && abs(tX - tX0) < 15 && abs(tY - tY0) < 15) {
      tLongSent = true;
      push(IN_LONG_TOUCH, tX, tY);
    }
  } else if (!down && tDown) {                // released
    tDown = false;
    if (tLongSent) return;
    int dx = tX - tX0, dy = tY - tY0;
    if (abs(dx) >= SWIPE_MIN_PX || abs(dy) >= SWIPE_MIN_PX) {
      if (abs(dx) > abs(dy)) push(dx < 0 ? IN_SWIPE_L : IN_SWIPE_R);
      else                   push(dy < 0 ? IN_SWIPE_U : IN_SWIPE_D);
    } else {
      push(IN_TAP, tX, tY);
    }
  }
}

// ------------------------------------------------------------------ knob button
static void buttonPoll() {
  static bool pressed = false, longSent = false, lastRaw = false;
  static uint32_t tChange = 0, tPress = 0;
  uint32_t now = millis();
  bool raw = digitalRead(PIN_ENC_SW) == LOW;     // active low
  if (raw != lastRaw) { lastRaw = raw; tChange = now; }
  if (now - tChange < 25) return;                // debounce

  if (raw && !pressed) { pressed = true; longSent = false; tPress = now; }
  else if (raw && pressed && !longSent && now - tPress >= BTN_LONG_MS) {
    longSent = true; push(IN_BTN_LONG);
  } else if (!raw && pressed) {
    pressed = false;
    if (!longSent) push(IN_BTN_SHORT);
  }
}

// ------------------------------------------------------------------ public
void inputInit() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encState = ((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
  attachInterrupt(PIN_ENC_A, encISR, CHANGE);
  attachInterrupt(PIN_ENC_B, encISR, CHANGE);

  pinMode(PIN_TOUCH_RST, OUTPUT);
  pinMode(PIN_TOUCH_INT, INPUT);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
  touchReset();
}

bool inputPoll(InputEvent& ev) {
  touchPoll();
  buttonPoll();

  // Convert accumulated quadrature counts to detent events
  noInterrupts();
  int32_t c = encCount;
  if (abs(c) >= ENC_COUNTS_PER_DETENT) encCount -= (c > 0 ? 1 : -1) * ENC_COUNTS_PER_DETENT;
  interrupts();
  if (abs(c) >= ENC_COUNTS_PER_DETENT) {
    bool cw = (c > 0) != (ENC_REVERSE != 0);
    push(cw ? IN_ENC_CW : IN_ENC_CCW);
  }

  if (fTail == fHead) return false;
  ev = fifo[fTail];
  fTail = (fTail + 1) & 15;
  return true;
}
