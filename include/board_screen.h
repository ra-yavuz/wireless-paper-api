#pragma once
#include "protocol.h"

#ifdef WIRELESS_TRACKER
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
using GFX = Adafruit_GFX;
constexpr uint16_t WHITE = 0, BLACK = 1;
constexpr const char* BOARD_MODEL = "Heltec Wireless Tracker V1.1";
constexpr const char* AP_PREFIX = "WirelessTracker-";
constexpr const char* HOST_PREFIX = "wireless-tracker-";
inline void radioPowerOn() {} // SX1262 has its own supply on the Tracker.
class Screen {
 public:
  bool timeout = false;
  void begin() {
    pinMode(3, OUTPUT); digitalWrite(3, HIGH);
    // GNSS shares the TFT supply. Hold it in reset while this application runs.
    pinMode(35, OUTPUT); digitalWrite(35, LOW);
    pinMode(21, OUTPUT); digitalWrite(21, LOW);
    spi.begin(41, -1, 42, 38);
    tft.initR(INITR_MINI160x80);
    tft.setColRowStart(26, 1);
    tft.setRotation(1);
    tft.invertDisplay(true);
    tft.fillScreen(ST77XX_WHITE);
    digitalWrite(21, HIGH);
  }
  void show(const uint8_t* pixels) {
    uint16_t row[paper::WIDTH];
    tft.startWrite();
    tft.setAddrWindow(0, 0, paper::WIDTH, paper::HEIGHT);
    for (size_t y = 0; y < paper::HEIGHT; ++y) {
      for (size_t x = 0; x < paper::WIDTH; ++x)
        row[x] = pixels[y * paper::STRIDE + x / 8] & (0x80 >> (x % 8)) ? ST77XX_BLACK : ST77XX_WHITE;
      tft.writePixels(row, paper::WIDTH);
    }
    tft.endWrite();
  }
 private:
  SPIClass spi{HSPI};
  // Panel offsets are exposed by the ST7735 base class as a protected method.
  class Panel : public Adafruit_ST7735 {
   public:
    using Adafruit_ST7735::Adafruit_ST7735;
    using Adafruit_ST7735::setColRowStart;
  } tft{&spi, 38, 40, 39};
};
#else
#include <heltec-eink-modules.h>
constexpr const char* BOARD_MODEL = "Heltec Wireless Paper";
constexpr const char* AP_PREFIX = "WirelessPaper-";
constexpr const char* HOST_PREFIX = "wireless-paper-";
inline void radioPowerOn() { Platform::VExtOn(); }
#if PAPER_REVISION == 12
using Panel = EInkDisplay_WirelessPaperV1_2;
#else
using Panel = EInkDisplay_WirelessPaperV1_1;
#endif
class Screen : public Panel {
 public:
  bool timeout = false;
  void begin() { setRotation(1); }
  void show(const uint8_t* pixels) {
    fastmodeOff(); fillScreen(WHITE);
    for (size_t y = 0; y < paper::HEIGHT; ++y)
      for (size_t x = 0; x < paper::WIDTH; ++x)
        if (pixels[y * paper::STRIDE + x / 8] & (0x80 >> (x % 8))) drawPixel(x, y, BLACK);
    update(); wait();
#if PAPER_REVISION == 12
    sendCommand(0x10); sendData(0x01);
#else
    sendCommand(0x07); sendData(0xa5);
#endif
  }
 protected:
  void wait() override {
    uint32_t start = millis();
#if PAPER_REVISION == 12
    const int busy = HIGH;
#else
    const int busy = LOW;
#endif
    while (digitalRead(7) == busy) {
      if (millis() - start > 15000) { timeout = true; break; }
      delay(1);
    }
  }
};
#endif
