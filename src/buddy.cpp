#include "buddy.h"
#include <esp_system.h>

static LGFX_Sprite* spr = nullptr;
static eyes::Engine eng(1);
static uint32_t lastFrame = 0;
static bool engSeeded = false;

bool buddyInit(LGFX& tft) {
  spr = new LGFX_Sprite(&tft);
  spr->setColorDepth(16);
  spr->setPsram(true);
  if (!spr->createSprite(240, 240)) {
    delete spr;
    spr = nullptr;
    return false;
  }
  return true;
}

bool buddyAvailable() { return spr != nullptr; }

void buddyEnter(uint32_t nowMs) {
  if (!engSeeded) { eng = eyes::Engine(esp_random()); engSeeded = true; }
  lastFrame = nowMs - 100;           // draw right away
  eng.blink(nowMs);                  // a little blink on waking up
}

void buddyLook(float x, float y, uint32_t nowMs) { eng.lookAt(x, y, nowMs, 900); }
void buddyPoke(uint32_t nowMs) { eng.poke(nowMs); }

bool buddyFrame(uint32_t nowMs, const BuddyView& v) {
  if (!spr) return false;
  if ((uint32_t)(nowMs - lastFrame) < 33) return false;
  lastFrame = nowMs;

  eng.setMood(v.mood);
  eng.update(nowMs);
  eyes::draw(*spr, eng, (uint16_t)0);

  if (v.banner && *v.banner) {
    spr->setFont(&lgfx::fonts::Font2);
    spr->setTextDatum(lgfx::textdatum::middle_center);
    spr->setTextColor(v.bannerColor, (uint16_t)0);
    spr->drawString(v.banner, 120, 196);
  }
  if (v.toast && *v.toast) {
    spr->setFont(&lgfx::fonts::Font4);
    spr->setTextDatum(lgfx::textdatum::middle_center);
    int w = spr->textWidth(v.toast) + 24;
    if (w > 220) w = 220;
    spr->fillRoundRect(120 - w / 2, 168, w, 34, 14, eyes::rgb565(32, 34, 40));
    spr->setTextColor((uint16_t)0xFFFF, eyes::rgb565(32, 34, 40));
    spr->drawString(v.toast, 120, 185);
  }
  spr->pushSprite(0, 0);
  return true;
}
