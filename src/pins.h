#pragma once
// Elecrow CrowPanel 1.28" ESP32-S3 round display — pin map (from Elecrow wiki).
// Verify against your board revision before changing anything here.

// GC9A01 display (SPI)
#define PIN_TFT_SCLK 10
#define PIN_TFT_MOSI 11
#define PIN_TFT_MISO -1
#define PIN_TFT_DC   3
#define PIN_TFT_CS   9
#define PIN_TFT_RST  14
#define PIN_TFT_BL   46   // backlight, PWM

// CST816D touch (I2C)
#define PIN_TOUCH_SDA 6
#define PIN_TOUCH_SCL 7
#define PIN_TOUCH_INT 5
#define PIN_TOUCH_RST 13

// Spare I2C header (wiki labels it "OLED SSD1306")
#define PIN_I2C_SDA 38
#define PIN_I2C_SCL 39

// WS2812 ring
#define PIN_LED_DATA 48
#define LED_COUNT    5

// Rotary encoder
#define PIN_ENC_A  45
#define PIN_ENC_B  42
#define PIN_ENC_SW 41

// Power LED
#define PIN_PWR_LED 40

// Free test IOs. UNVERIFIED as free — confirm on your board before wiring.
// Optional "USB power present" sense. Tap the UPS module's INPUT side (the wall
// adapter 5V), NOT the 5V going to the board: the UPS keeps that rail alive on
// battery, so it would always read high. Divider: 100k from adapter 5V to the
// pin, 150k from the pin to GND (~3.0V, above the S3's 2.5V logic-high
// threshold; a 100k/100k split gives 2.5V and is too marginal).
// HIGH = adapter present, LOW = running on battery.
#define PIN_VBUS_SENSE 4

#define SCREEN_W 240
#define SCREEN_H 240
