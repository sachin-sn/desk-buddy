#include "power.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "hid.h"
#include "log.h"
#include "pins.h"
#include "settings.h"

static volatile bool offReq = false, uiParked = false;

void powerRequestOff()   { offReq = true; }
bool powerOffRequested() { return offReq; }
bool powerUiParked()     { return uiParked; }
void powerSetUiParked()  { uiParked = true; }

void powerDown() {
  logEvent("power: turning off; press the knob to wake it");
  delay(400);                                          // let a pending web reply leave first
  for (uint32_t t0 = millis(); !uiParked && millis() - t0 < 1500;) delay(10);   // screen and LEDs go dark first

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (gCfg.bleHid) hidEnd();

  gpio_reset_pin((gpio_num_t)PIN_TFT_BL);              // backlight off for good (was driven by PWM)
  pinMode(PIN_TFT_BL, OUTPUT);     digitalWrite(PIN_TFT_BL, LOW);
  digitalWrite(PIN_LCD_PWR, LOW);                      // LCD and touch 3.3V
  digitalWrite(PIN_EXT5V_PWR, LOW);                    // external connectors' 5V
  digitalWrite(PIN_PWR_LED, HIGH);                     // this LED is lit when LOW
  Serial.flush();

  // The press that turned us off may still be held: wait for it to be released, or we would wake straight away.
  for (uint32_t t0 = millis(), up = 0; millis() - t0 < 6000;) {
    if (digitalRead(PIN_ENC_SW) == HIGH) { if (!up) up = millis(); if (millis() - up > 250) break; }
    else up = 0;
    delay(10);
  }

  gpio_wakeup_enable((gpio_num_t)PIN_ENC_SW, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();                             // returns when the knob is pressed
  ESP.restart();                                       // simplest way back to a clean, fully working buddy
}
