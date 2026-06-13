#ifdef PANEL_CYD_TFT

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "CYD/CydMatrix.h"
#include "CYD/CydTouch.h"
#include "Aquarium.h"
#include "SCD40.h"
#include "StateManager.h"

#if __has_include("WifiSecrets.h")
#include "WifiSecrets.h"
#endif

#ifndef CYD_TOUCH_DEBUG
#define CYD_TOUCH_DEBUG 0
#endif

#ifndef CYD_STARTUP_SMOKE_TEST
#define CYD_STARTUP_SMOKE_TEST 0
#endif

#ifndef CYD_FRAMEBUFFER_RENDERER
#define CYD_FRAMEBUFFER_RENDERER 0
#endif

#ifndef CYD_PERF_DEBUG
#define CYD_PERF_DEBUG 0
#endif

#ifndef CYD_TFT_DIAGNOSTIC
#define CYD_TFT_DIAGNOSTIC 0
#endif

#ifndef CYD_REFERENCE_FRAME_MODE
#define CYD_REFERENCE_FRAME_MODE 0
#endif

#ifndef CYD_REFERENCE_FRAME_BRIGHTNESS
#define CYD_REFERENCE_FRAME_BRIGHTNESS 255
#endif

#ifndef CYD_PREVIEW_BRIGHTNESS
#define CYD_PREVIEW_BRIGHTNESS 100
#endif

#ifndef CYD_WIFI_TIME
#define CYD_WIFI_TIME 1
#endif

#ifndef CYD_SHOW_CLOCK
#define CYD_SHOW_CLOCK 1
#endif

#ifndef CYD_WIFI_SSID
#define CYD_WIFI_SSID ""
#endif

#ifndef CYD_WIFI_PASSWORD
#define CYD_WIFI_PASSWORD ""
#endif

#ifndef CYD_TIMEZONE
#define CYD_TIMEZONE "CST6CDT,M3.2.0/2,M11.1.0/2"
#endif

#ifndef CYD_WIFI_CONNECT_TIMEOUT_MS
#define CYD_WIFI_CONNECT_TIMEOUT_MS 8000
#endif

#ifndef CYD_NTP_SYNC_TIMEOUT_MS
#define CYD_NTP_SYNC_TIMEOUT_MS 5000
#endif

#ifndef CYD_NTP_RESYNC_INTERVAL_MS
#define CYD_NTP_RESYNC_INTERVAL_MS 21600000UL
#endif

#ifndef CYD_NTP_RETRY_INTERVAL_MS
#define CYD_NTP_RETRY_INTERVAL_MS 300000UL
#endif

#ifndef CYD_WIFI_DISCONNECT_AFTER_NTP
#define CYD_WIFI_DISCONNECT_AFTER_NTP 1
#endif

#ifndef CYD_LIGHT_SENSOR_DIAGNOSTIC
#define CYD_LIGHT_SENSOR_DIAGNOSTIC 0
#endif

#ifndef CYD_LIGHT_SENSOR_SCREEN_DIAGNOSTIC
#define CYD_LIGHT_SENSOR_SCREEN_DIAGNOSTIC 0
#endif

#ifndef CYD_LIGHT_SENSOR_DIAGNOSTIC_INTERVAL_MS
#define CYD_LIGHT_SENSOR_DIAGNOSTIC_INTERVAL_MS 1000
#endif

#ifndef CYD_AUTO_BACKLIGHT
#define CYD_AUTO_BACKLIGHT 0
#endif

#ifndef CYD_AUTO_BACKLIGHT_SENSOR_PIN
#define CYD_AUTO_BACKLIGHT_SENSOR_PIN 34
#endif

#ifndef CYD_AUTO_BACKLIGHT_BRIGHT_RAW
#define CYD_AUTO_BACKLIGHT_BRIGHT_RAW 10
#endif

#ifndef CYD_AUTO_BACKLIGHT_DARK_RAW
#define CYD_AUTO_BACKLIGHT_DARK_RAW 650
#endif

#ifndef CYD_AUTO_BACKLIGHT_MIN_PERCENT
#define CYD_AUTO_BACKLIGHT_MIN_PERCENT 38
#endif

#ifndef CYD_AUTO_BACKLIGHT_MAX_PERCENT
#define CYD_AUTO_BACKLIGHT_MAX_PERCENT 100
#endif

#ifndef CYD_AUTO_BACKLIGHT_READ_INTERVAL_MS
#define CYD_AUTO_BACKLIGHT_READ_INTERVAL_MS 750
#endif

#ifndef CYD_AUTO_BACKLIGHT_STEP_PERCENT
#define CYD_AUTO_BACKLIGHT_STEP_PERCENT 4
#endif

#ifndef CYD_AUTO_BACKLIGHT_REPORT_INTERVAL_MS
#define CYD_AUTO_BACKLIGHT_REPORT_INTERVAL_MS 10000
#endif

#ifndef CYD_AUTONOMOUS_LIFE
#define CYD_AUTONOMOUS_LIFE 0
#endif

#ifndef CYD_AUTONOMOUS_FOOD_MIN_MS
#define CYD_AUTONOMOUS_FOOD_MIN_MS 7000
#endif

#ifndef CYD_AUTONOMOUS_FOOD_MAX_MS
#define CYD_AUTONOMOUS_FOOD_MAX_MS 16000
#endif

#ifndef CYD_AUTONOMOUS_MAX_FOOD
#define CYD_AUTONOMOUS_MAX_FOOD 3
#endif

#if CYD_REFERENCE_FRAME_MODE

#include "CydReferenceFrame.h"

namespace {

CydMatrix referenceMatrix;

void drawReferenceFrame() {
  if (CYD_REFERENCE_FRAME_WIDTH != CydMatrixSettings::LOGICAL_WIDTH ||
      CYD_REFERENCE_FRAME_HEIGHT != CydMatrixSettings::LOGICAL_HEIGHT) {
    Serial.printf(
        "reference frame size mismatch ref=%ux%u matrix=%ux%u; abort draw\n",
        CYD_REFERENCE_FRAME_WIDTH, CYD_REFERENCE_FRAME_HEIGHT,
        CydMatrixSettings::LOGICAL_WIDTH, CydMatrixSettings::LOGICAL_HEIGHT);
    return;
  }

  for (uint16_t y = 0; y < CYD_REFERENCE_FRAME_HEIGHT; ++y) {
    for (uint16_t x = 0; x < CYD_REFERENCE_FRAME_WIDTH; ++x) {
      const uint32_t pixelIndex =
          static_cast<uint32_t>(y) * CYD_REFERENCE_FRAME_WIDTH + x;
      const uint32_t byteIndex = pixelIndex * 3;
      const uint8_t r = pgm_read_byte(&CYD_REFERENCE_FRAME_RGB888[byteIndex]);
      const uint8_t g =
          pgm_read_byte(&CYD_REFERENCE_FRAME_RGB888[byteIndex + 1]);
      const uint8_t b =
          pgm_read_byte(&CYD_REFERENCE_FRAME_RGB888[byteIndex + 2]);
      const bool foreground =
          pgm_read_byte(&CYD_REFERENCE_FRAME_LAYER[pixelIndex]) ==
          CYD_REFERENCE_LAYER_FOREGROUND;
      referenceMatrix.drawPixelRGB888Profile(x, y, r, g, b, foreground);
    }
  }

  referenceMatrix.update();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("CYD reference frame boot");
  Serial.printf("reference mode=%s logical=%ux%u nonblack=%u bytes=%u\n",
                CYD_REFERENCE_FRAME_MODE_ID, CYD_REFERENCE_FRAME_WIDTH,
                CYD_REFERENCE_FRAME_HEIGHT,
                CYD_REFERENCE_FRAME_NONBLACK_PIXELS,
                CYD_REFERENCE_FRAME_RGB888_BYTES);
  Serial.printf("reference foreground pixels=%u layer_bytes=%u\n",
                CYD_REFERENCE_FRAME_FOREGROUND_PIXELS,
                CYD_REFERENCE_FRAME_LAYER_BYTES);
  Serial.printf("reference brightness=%u/255\n",
                CYD_REFERENCE_FRAME_BRIGHTNESS);

  referenceMatrix.init();
  referenceMatrix.setBrightness(CYD_REFERENCE_FRAME_BRIGHTNESS);
  drawReferenceFrame();
  Serial.println("reference frame drawn");
}

void loop() {
  delay(1000);
}

#elif CYD_LIGHT_SENSOR_SCREEN_DIAGNOSTIC

#include <TFT_eSPI.h>

namespace {

TFT_eSPI lightDiagTft;

constexpr uint8_t LDR_PIN = 34;
constexpr uint8_t AUX_ADC_PIN = 35;
constexpr unsigned long SCREEN_UPDATE_INTERVAL_MS = 250;
constexpr unsigned long SERIAL_REPORT_INTERVAL_MS = 1000;

uint16_t min34_0db = 4095;
uint16_t max34_0db = 0;
uint16_t min34_11db = 4095;
uint16_t max34_11db = 0;
uint16_t min35_11db = 4095;
uint16_t max35_11db = 0;

uint16_t averageAnalogReadAt0db(uint8_t pin) {
  analogSetPinAttenuation(pin, ADC_0db);
  delayMicroseconds(500);
  (void)analogRead(pin);

  uint32_t total = 0;
  constexpr uint8_t samples = 16;
  for (uint8_t i = 0; i < samples; ++i) {
    total += analogRead(pin);
    delayMicroseconds(150);
  }
  return static_cast<uint16_t>(total / samples);
}

uint16_t averageAnalogReadAt11db(uint8_t pin) {
  analogSetPinAttenuation(pin, ADC_11db);
  delayMicroseconds(500);
  (void)analogRead(pin);

  uint32_t total = 0;
  constexpr uint8_t samples = 16;
  for (uint8_t i = 0; i < samples; ++i) {
    total += analogRead(pin);
    delayMicroseconds(150);
  }
  return static_cast<uint16_t>(total / samples);
}

uint32_t averageAnalogMilliVoltsAt11db(uint8_t pin) {
  analogSetPinAttenuation(pin, ADC_11db);
  delayMicroseconds(500);
  (void)analogReadMilliVolts(pin);

  uint32_t total = 0;
  constexpr uint8_t samples = 8;
  for (uint8_t i = 0; i < samples; ++i) {
    total += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  return total / samples;
}

void drawHorizontalBar(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint16_t value, uint16_t color) {
  lightDiagTft.drawRect(x, y, w, h, TFT_DARKGREY);
  lightDiagTft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
  const int16_t fillW =
      static_cast<int16_t>(((static_cast<uint32_t>(w - 2) * value) + 2047) /
                           4095);
  if (fillW > 0) {
    lightDiagTft.fillRect(x + 1, y + 1, fillW, h - 2, color);
  }
}

void drawLightDiagnosticScreen(uint16_t raw34_0db, uint16_t raw34_11db,
                               uint32_t mv34_11db, uint16_t raw35_11db) {
  lightDiagTft.fillScreen(TFT_BLACK);
  lightDiagTft.setTextDatum(TL_DATUM);
  lightDiagTft.setTextWrap(false);

  lightDiagTft.setTextColor(TFT_CYAN, TFT_BLACK);
  lightDiagTft.setTextSize(2);
  lightDiagTft.setCursor(8, 8);
  lightDiagTft.print("LDR / IO34 test");

  lightDiagTft.setTextSize(1);
  lightDiagTft.setTextColor(TFT_WHITE, TFT_BLACK);
  lightDiagTft.setCursor(8, 34);
  lightDiagTft.print("Schematic: GT36516 -> IO34");
  lightDiagTft.setCursor(8, 48);
  lightDiagTft.print("Cover sensor: value should rise");
  lightDiagTft.setCursor(8, 62);
  lightDiagTft.print("Flashlight: value should fall");

  char line[64] = {};

  lightDiagTft.setTextColor(TFT_YELLOW, TFT_BLACK);
  snprintf(line, sizeof(line), "GPIO34 0dB raw: %4u", raw34_0db);
  lightDiagTft.setCursor(8, 88);
  lightDiagTft.print(line);
  drawHorizontalBar(8, 104, 224, 14, raw34_0db, TFT_YELLOW);

  lightDiagTft.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(line, sizeof(line), "range: %4u-%4u  delta:%4u", min34_0db,
           max34_0db, max34_0db - min34_0db);
  lightDiagTft.setCursor(8, 124);
  lightDiagTft.print(line);

  lightDiagTft.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(line, sizeof(line), "GPIO34 11dB raw:%4u  %4lu mV", raw34_11db,
           static_cast<unsigned long>(mv34_11db));
  lightDiagTft.setCursor(8, 150);
  lightDiagTft.print(line);
  drawHorizontalBar(8, 166, 224, 14, raw34_11db, TFT_CYAN);

  lightDiagTft.setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(line, sizeof(line), "range: %4u-%4u  delta:%4u", min34_11db,
           max34_11db, max34_11db - min34_11db);
  lightDiagTft.setCursor(8, 186);
  lightDiagTft.print(line);

  lightDiagTft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  snprintf(line, sizeof(line), "GPIO35 fallback: %4u", raw35_11db);
  lightDiagTft.setCursor(8, 218);
  lightDiagTft.print(line);
  snprintf(line, sizeof(line), "range: %4u-%4u  delta:%4u", min35_11db,
           max35_11db, max35_11db - min35_11db);
  lightDiagTft.setCursor(8, 232);
  lightDiagTft.print(line);

  lightDiagTft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lightDiagTft.setCursor(8, 270);
  lightDiagTft.print("If delta stays 0: cover tighter");
  lightDiagTft.setCursor(8, 284);
  lightDiagTft.print("or board LDR circuit is not active.");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("CYD light sensor screen diagnostic boot");
  Serial.println("expected circuit: GT36516 LDR -> GPIO34");
  Serial.println("cover sensor should increase GPIO34; flashlight should lower it");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  lightDiagTft.init();
  lightDiagTft.setRotation(0);
  lightDiagTft.setSwapBytes(CYD_TFT_SWAP_BYTES != 0);
  lightDiagTft.fillScreen(TFT_BLACK);

  analogReadResolution(12);
  pinMode(LDR_PIN, INPUT);
  pinMode(AUX_ADC_PIN, INPUT);
}

void loop() {
  static unsigned long lastScreenMs = 0;
  static unsigned long lastSerialMs = 0;

  const unsigned long now = millis();
  if (now - lastScreenMs < SCREEN_UPDATE_INTERVAL_MS) {
    delay(5);
    return;
  }
  lastScreenMs = now;

  const uint16_t raw34_0db = averageAnalogReadAt0db(LDR_PIN);
  const uint16_t raw34_11db = averageAnalogReadAt11db(LDR_PIN);
  const uint32_t mv34_11db = averageAnalogMilliVoltsAt11db(LDR_PIN);
  const uint16_t raw35_11db = averageAnalogReadAt11db(AUX_ADC_PIN);

  min34_0db = min(min34_0db, raw34_0db);
  max34_0db = max(max34_0db, raw34_0db);
  min34_11db = min(min34_11db, raw34_11db);
  max34_11db = max(max34_11db, raw34_11db);
  min35_11db = min(min35_11db, raw35_11db);
  max35_11db = max(max35_11db, raw35_11db);

  drawLightDiagnosticScreen(raw34_0db, raw34_11db, mv34_11db, raw35_11db);

  if (now - lastSerialMs >= SERIAL_REPORT_INTERVAL_MS) {
    lastSerialMs = now;
    Serial.printf(
        "light_screen_diag: gpio34_0db=%u range=%u-%u delta=%u "
        "gpio34_11db=%u mv=%lu range=%u-%u delta=%u gpio35_11db=%u "
        "range=%u-%u delta=%u\n",
        raw34_0db, min34_0db, max34_0db, max34_0db - min34_0db, raw34_11db,
        static_cast<unsigned long>(mv34_11db), min34_11db, max34_11db,
        max34_11db - min34_11db, raw35_11db, min35_11db, max35_11db,
        max35_11db - min35_11db);
  }
}

#elif CYD_TFT_DIAGNOSTIC

#include <TFT_eSPI.h>

namespace {

TFT_eSPI diagTft;

void drawCell(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color,
              const char* label, uint16_t labelColor) {
  diagTft.fillRect(x, y, w, h, color);
  diagTft.drawRect(x, y, w, h, TFT_WHITE);
  diagTft.setTextColor(labelColor, color);
  diagTft.setTextSize(3);
  diagTft.setCursor(x + 14, y + 18);
  diagTft.print(label);
}

void drawCalibrationChart() {
  diagTft.fillScreen(TFT_BLACK);

  constexpr int16_t cellW = 120;
  constexpr int16_t cellH = 70;

  drawCell(0, 0, cellW, cellH, TFT_RED, "A", TFT_WHITE);
  drawCell(120, 0, cellW, cellH, TFT_GREEN, "B", TFT_BLACK);
  drawCell(0, 70, cellW, cellH, TFT_BLUE, "C", TFT_WHITE);
  drawCell(120, 70, cellW, cellH, TFT_WHITE, "D", TFT_BLACK);
  drawCell(0, 140, cellW, cellH, TFT_BLACK, "E", TFT_WHITE);
  drawCell(120, 140, cellW, cellH, TFT_YELLOW, "F", TFT_BLACK);
  drawCell(0, 210, cellW, cellH, TFT_CYAN, "G", TFT_BLACK);
  drawCell(120, 210, cellW, cellH, TFT_MAGENTA, "H", TFT_WHITE);

  constexpr int16_t grayTop = 280;
  constexpr int16_t grayH = 40;
  constexpr int16_t bars = 8;
  constexpr int16_t barW = 240 / bars;
  for (int16_t i = 0; i < bars; ++i) {
    const uint8_t level = static_cast<uint8_t>((255 * i) / (bars - 1));
    diagTft.fillRect(i * barW, grayTop, barW, grayH,
                     diagTft.color565(level, level, level));
  }
  diagTft.drawRect(0, grayTop, 240, grayH, TFT_WHITE);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("CYD TFT diagnostic boot");
  Serial.printf("TFT pins miso=%d mosi=%d sclk=%d cs=%d dc=%d rst=%d bl=%d\n",
                TFT_MISO, TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST,
                TFT_BL);
  Serial.printf("backlight_on=%s spi=%lu read_spi=%lu touch_spi=%lu\n",
                TFT_BACKLIGHT_ON == HIGH ? "HIGH" : "LOW",
                static_cast<unsigned long>(SPI_FREQUENCY),
                static_cast<unsigned long>(SPI_READ_FREQUENCY),
                static_cast<unsigned long>(SPI_TOUCH_FREQUENCY));

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  diagTft.init();
  diagTft.setRotation(0);
  drawCalibrationChart();
  Serial.println("color calibration chart fixed:");
  Serial.println("A=RED B=GREEN C=BLUE D=WHITE E=BLACK F=YELLOW G=CYAN H=MAGENTA");
}

void loop() {
  delay(1000);
}

#else

constexpr unsigned long STATE_SAVE_INTERVAL_MINUTES = 30;
constexpr uint8_t IDEAL_FPS = 30;
constexpr unsigned long FRAME_INTERVAL_MS = 1000 / IDEAL_FPS;
constexpr uint8_t FIXED_HUMIDITY_PERCENT = 50;
constexpr unsigned long PERF_REPORT_INTERVAL_MS = 2000;

StateManager stateManager(STATE_SAVE_INTERVAL_MINUTES);

namespace {

CydMatrix matrix;
CydTouch touch;
SCD40 scd40;
Aquarium aquarium(&matrix, &scd40, &stateManager);
time_t clockBaseEpoch = 0;
unsigned long clockBaseMillis = 0;

enum class ClockSyncState : uint8_t {
  Idle,
  Connecting,
  WaitingForNtp,
};

ClockSyncState clockSyncState = ClockSyncState::Idle;
unsigned long nextClockSyncMs = 0;
unsigned long clockSyncStartedMs = 0;
unsigned long clockNtpWaitStartedMs = 0;
const char* activeClockSyncReason = "boot";
bool ntpEverSynced = false;

#if CYD_AUTO_BACKLIGHT
uint16_t ambientRawEma = 0;
uint8_t ambientBacklightPercent = CYD_AUTO_BACKLIGHT_MAX_PERCENT;
bool ambientBacklightReady = false;
#endif

struct PixelGlyph {
  const char* rows[5];
};

PixelGlyph glyphFor(char rawChar) {
  const char c = static_cast<char>(toupper(rawChar));
  switch (c) {
    case '0': return {{"111", "101", "101", "101", "111"}};
    case '1': return {{"010", "110", "010", "010", "111"}};
    case '2': return {{"111", "001", "111", "100", "111"}};
    case '3': return {{"111", "001", "111", "001", "111"}};
    case '4': return {{"101", "101", "111", "001", "001"}};
    case '5': return {{"111", "100", "111", "001", "111"}};
    case '6': return {{"111", "100", "111", "101", "111"}};
    case '7': return {{"111", "001", "010", "010", "010"}};
    case '8': return {{"111", "101", "111", "101", "111"}};
    case '9': return {{"111", "101", "111", "001", "111"}};
    case 'A': return {{"010", "101", "111", "101", "101"}};
    case 'B': return {{"110", "101", "110", "101", "110"}};
    case 'C': return {{"111", "100", "100", "100", "111"}};
    case 'D': return {{"110", "101", "101", "101", "110"}};
    case 'E': return {{"111", "100", "111", "100", "111"}};
    case 'F': return {{"111", "100", "111", "100", "100"}};
    case 'G': return {{"111", "100", "101", "101", "111"}};
    case 'H': return {{"101", "101", "111", "101", "101"}};
    case 'I': return {{"111", "010", "010", "010", "111"}};
    case 'J': return {{"001", "001", "001", "101", "111"}};
    case 'K': return {{"101", "101", "110", "101", "101"}};
    case 'L': return {{"100", "100", "100", "100", "111"}};
    case 'M': return {{"101", "111", "111", "101", "101"}};
    case 'N': return {{"101", "111", "111", "111", "101"}};
    case 'O': return {{"111", "101", "101", "101", "111"}};
    case 'P': return {{"111", "101", "111", "100", "100"}};
    case 'Q': return {{"111", "101", "101", "111", "001"}};
    case 'R': return {{"111", "101", "111", "110", "101"}};
    case 'S': return {{"111", "100", "111", "001", "111"}};
    case 'T': return {{"111", "010", "010", "010", "010"}};
    case 'U': return {{"101", "101", "101", "101", "111"}};
    case 'V': return {{"101", "101", "101", "101", "010"}};
    case 'W': return {{"101", "101", "111", "111", "101"}};
    case 'X': return {{"101", "101", "010", "101", "101"}};
    case 'Y': return {{"101", "101", "010", "010", "010"}};
    case 'Z': return {{"111", "001", "010", "100", "111"}};
    case ':': return {{"0", "1", "0", "1", "0"}};
    case '.': return {{"0", "0", "0", "0", "1"}};
    case '/': return {{"001", "001", "010", "100", "100"}};
    case '-': return {{"000", "000", "111", "000", "000"}};
    default: return {{"0", "0", "0", "0", "0"}};
  }
}

uint8_t glyphWidth(const PixelGlyph& glyph) {
  uint8_t width = 0;
  for (uint8_t row = 0; row < 5; ++row) {
    const uint8_t rowWidth = static_cast<uint8_t>(strlen(glyph.rows[row]));
    width = rowWidth > width ? rowWidth : width;
  }
  return width;
}

uint16_t pixelTextWidth(const char* text, uint8_t scale, uint8_t letterSpacing) {
  uint16_t width = 0;
  const size_t len = strlen(text);
  for (size_t i = 0; i < len; ++i) {
    width += glyphWidth(glyphFor(text[i])) * scale;
    if (i + 1 < len) {
      width += letterSpacing;
    }
  }
  return width;
}

void drawPixelText(const char* text, int16_t x, int16_t y, uint8_t scale,
                   uint8_t letterSpacing, uint16_t color) {
  if (matrix.foreground == nullptr) {
    return;
  }

  int16_t cursorX = x;
  const size_t len = strlen(text);
  for (size_t i = 0; i < len; ++i) {
    const PixelGlyph glyph = glyphFor(text[i]);
    const uint8_t width = glyphWidth(glyph);
    for (uint8_t row = 0; row < 5; ++row) {
      for (uint8_t col = 0; col < strlen(glyph.rows[row]); ++col) {
        if (glyph.rows[row][col] != '1') {
          continue;
        }
        matrix.foreground->fillRect(cursorX + col * scale, y + row * scale,
                                    scale, scale, color);
      }
    }
    cursorX += width * scale + letterSpacing;
  }
}

void drawCenteredPixelText(const char* text, int16_t centerX, int16_t y,
                           uint8_t scale, uint8_t letterSpacing,
                           uint16_t color, uint16_t shadowColor) {
  const uint16_t width = pixelTextWidth(text, scale, letterSpacing);
  const int16_t x = centerX - static_cast<int16_t>(width / 2);
  drawPixelText(text, x + 1, y + 1, scale, letterSpacing, shadowColor);
  drawPixelText(text, x, y, scale, letterSpacing, color);
}

int monthFromBuildName(const char* month) {
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i) {
    if (strncmp(month, months[i], 3) == 0) {
      return i;
    }
  }
  return 0;
}

time_t compileTimeEpoch() {
  char monthName[4] = {};
  int day = 1;
  int year = 2026;
  int hour = 0;
  int minute = 0;
  int second = 0;
  sscanf(__DATE__, "%3s %d %d", monthName, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  tm buildTime = {};
  buildTime.tm_year = year - 1900;
  buildTime.tm_mon = monthFromBuildName(monthName);
  buildTime.tm_mday = day;
  buildTime.tm_hour = hour;
  buildTime.tm_min = minute;
  buildTime.tm_sec = second;
  buildTime.tm_isdst = -1;
  return mktime(&buildTime);
}

void initClock() {
  clockBaseEpoch = compileTimeEpoch();
  clockBaseMillis = millis();
  Serial.printf("clock source=compile_time_fallback base=%s %s\n", __DATE__,
                __TIME__);
}

bool wifiCredentialsConfigured() {
  return strlen(CYD_WIFI_SSID) > 0 &&
         strcmp(CYD_WIFI_SSID, "Your WiFi name") != 0;
}

bool timeReached(unsigned long now, unsigned long target) {
  return static_cast<long>(now - target) >= 0;
}

bool systemTimeIsNtpValid(tm* localTime) {
  const time_t now = time(nullptr);
  localtime_r(&now, localTime);
  return localTime->tm_year > (2016 - 1900);
}

void scheduleClockSyncRetry(unsigned long delayMs) {
  nextClockSyncMs = millis() + delayMs;
}

void stopClockSyncWifiIfNeeded() {
#if CYD_WIFI_DISCONNECT_AFTER_NTP
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("wifi disconnected after ntp");
#endif
}

void startClockSync(const char* reason) {
#if CYD_WIFI_TIME
  if (!wifiCredentialsConfigured()) {
    static bool reportedMissingCredentials = false;
    if (!reportedMissingCredentials) {
      Serial.println("clock ntp=skipped reason=no_wifi_credentials");
      reportedMissingCredentials = true;
    }
    nextClockSyncMs = 0;
    return;
  }

  if (clockSyncState != ClockSyncState::Idle) {
    return;
  }

  activeClockSyncReason = reason;
  Serial.printf("clock ntp=start reason=%s ssid=\"%s\" timeout=%lu ms\n",
                activeClockSyncReason,
                CYD_WIFI_SSID,
                static_cast<unsigned long>(CYD_WIFI_CONNECT_TIMEOUT_MS));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (WiFi.status() == WL_CONNECTED) {
    clockSyncState = ClockSyncState::WaitingForNtp;
    clockNtpWaitStartedMs = millis();
    configTzTime(CYD_TIMEZONE, "pool.ntp.org", "time.nist.gov",
                 "time.google.com");
    return;
  }

  WiFi.begin(CYD_WIFI_SSID, CYD_WIFI_PASSWORD);
  clockSyncStartedMs = millis();
  clockSyncState = ClockSyncState::Connecting;
#else
  (void)reason;
  Serial.println("clock ntp=disabled");
#endif
}

void updateClockSync() {
#if CYD_WIFI_TIME
  const unsigned long now = millis();
  if (clockSyncState == ClockSyncState::Idle) {
    if (nextClockSyncMs == 0 || timeReached(now, nextClockSyncMs)) {
      startClockSync(ntpEverSynced ? "scheduled" : "boot");
    }
    return;
  }

  if (clockSyncState == ClockSyncState::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress localIp = WiFi.localIP();
      Serial.printf("wifi connected ip=%s reason=%s\n",
                    localIp.toString().c_str(), activeClockSyncReason);
      configTzTime(CYD_TIMEZONE, "pool.ntp.org", "time.nist.gov",
                   "time.google.com");
      clockNtpWaitStartedMs = now;
      clockSyncState = ClockSyncState::WaitingForNtp;
      return;
    }

    if (now - clockSyncStartedMs >= CYD_WIFI_CONNECT_TIMEOUT_MS) {
      Serial.printf(
          "clock ntp=skipped reason=wifi_connect_failed status=%d "
          "retry_ms=%lu\n",
          WiFi.status(), static_cast<unsigned long>(CYD_NTP_RETRY_INTERVAL_MS));
      clockSyncState = ClockSyncState::Idle;
      scheduleClockSyncRetry(CYD_NTP_RETRY_INTERVAL_MS);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    return;
  }

  tm ntpTime = {};
  if (systemTimeIsNtpValid(&ntpTime)) {
    clockBaseEpoch = time(nullptr);
    clockBaseMillis = millis();
    ntpEverSynced = true;
    Serial.printf(
        "clock source=ntp reason=%s local=%04d-%02d-%02d %02d:%02d:%02d "
        "next_sync_ms=%lu\n",
        activeClockSyncReason, ntpTime.tm_year + 1900, ntpTime.tm_mon + 1,
        ntpTime.tm_mday, ntpTime.tm_hour, ntpTime.tm_min, ntpTime.tm_sec,
        static_cast<unsigned long>(CYD_NTP_RESYNC_INTERVAL_MS));
    clockSyncState = ClockSyncState::Idle;
    scheduleClockSyncRetry(CYD_NTP_RESYNC_INTERVAL_MS);
    stopClockSyncWifiIfNeeded();
    return;
  }

  if (now - clockNtpWaitStartedMs >= CYD_NTP_SYNC_TIMEOUT_MS) {
    Serial.printf(
        "clock ntp=failed reason=sync_timeout fallback=compile_time "
        "retry_ms=%lu\n",
        static_cast<unsigned long>(CYD_NTP_RETRY_INTERVAL_MS));
    clockSyncState = ClockSyncState::Idle;
    scheduleClockSyncRetry(CYD_NTP_RETRY_INTERVAL_MS);
    stopClockSyncWifiIfNeeded();
  }
#endif
}

uint16_t averageAnalogRead(uint8_t pin) {
  uint32_t total = 0;
  constexpr uint8_t samples = 8;
  for (uint8_t i = 0; i < samples; ++i) {
    total += analogRead(pin);
    delayMicroseconds(200);
  }
  return static_cast<uint16_t>(total / samples);
}

void setupLightSensorDiagnostic() {
#if CYD_LIGHT_SENSOR_DIAGNOSTIC
  analogReadResolution(12);
  analogSetPinAttenuation(34, ADC_11db);
  analogSetPinAttenuation(35, ADC_11db);
  pinMode(34, INPUT);
  pinMode(35, INPUT);
  Serial.println(
      "light_diag=on candidates=gpio34,gpio35 action=cover_or_flashlight");
#endif
}

void updateLightSensorDiagnostic() {
#if CYD_LIGHT_SENSOR_DIAGNOSTIC
  static unsigned long lastReportMs = 0;
  static uint16_t min34 = 4095;
  static uint16_t max34 = 0;
  static uint16_t min35 = 4095;
  static uint16_t max35 = 0;

  const unsigned long now = millis();
  if (now - lastReportMs < CYD_LIGHT_SENSOR_DIAGNOSTIC_INTERVAL_MS) {
    return;
  }
  lastReportMs = now;

  const uint16_t raw34 = averageAnalogRead(34);
  const uint16_t raw35 = averageAnalogRead(35);
  min34 = min(min34, raw34);
  max34 = max(max34, raw34);
  min35 = min(min35, raw35);
  max35 = max(max35, raw35);

  Serial.printf(
      "light_diag: gpio34=%u range=%u-%u delta=%u gpio35=%u range=%u-%u "
      "delta=%u\n",
      raw34, min34, max34, max34 - min34, raw35, min35, max35,
      max35 - min35);
#endif
}

#if CYD_AUTO_BACKLIGHT
uint16_t readAmbientLightRaw() {
  uint32_t total = 0;
  constexpr uint8_t samples = 16;
  for (uint8_t i = 0; i < samples; ++i) {
    total += analogRead(CYD_AUTO_BACKLIGHT_SENSOR_PIN);
    delayMicroseconds(150);
  }
  return static_cast<uint16_t>(total / samples);
}

uint8_t mapAmbientRawToBacklight(uint16_t raw) {
  constexpr uint16_t brightRaw = CYD_AUTO_BACKLIGHT_BRIGHT_RAW;
  constexpr uint16_t darkRaw = CYD_AUTO_BACKLIGHT_DARK_RAW;
  constexpr uint8_t minPercent = CYD_AUTO_BACKLIGHT_MIN_PERCENT;
  constexpr uint8_t maxPercent = CYD_AUTO_BACKLIGHT_MAX_PERCENT;

  if (darkRaw <= brightRaw) {
    return maxPercent;
  }
  if (raw <= brightRaw) {
    return maxPercent;
  }
  if (raw >= darkRaw) {
    return minPercent;
  }

  const uint16_t span = darkRaw - brightRaw;
  const uint16_t darkness = raw - brightRaw;
  const uint8_t range = maxPercent - minPercent;
  return maxPercent -
         static_cast<uint8_t>(
             (static_cast<uint32_t>(darkness) * range + span / 2) / span);
}

uint8_t stepBacklightToward(uint8_t current, uint8_t target) {
  constexpr uint8_t step = CYD_AUTO_BACKLIGHT_STEP_PERCENT;
  if (current < target) {
    const uint16_t next = static_cast<uint16_t>(current) + step;
    return next > target ? target : static_cast<uint8_t>(next);
  }
  if (current > target) {
    return current > target + step ? current - step : target;
  }
  return current;
}

void setupAmbientBacklight() {
  analogReadResolution(12);
  analogSetPinAttenuation(CYD_AUTO_BACKLIGHT_SENSOR_PIN, ADC_0db);
  pinMode(CYD_AUTO_BACKLIGHT_SENSOR_PIN, INPUT);

  const uint16_t raw = readAmbientLightRaw();
  ambientRawEma = raw;
  ambientBacklightPercent = mapAmbientRawToBacklight(raw);
  ambientBacklightReady = true;
  matrix.setBacklightPercent(ambientBacklightPercent);
  Serial.printf(
      "auto_backlight=on pin=%u raw=%u bright_raw=%u dark_raw=%u "
      "percent=%u range=%u-%u\n",
      CYD_AUTO_BACKLIGHT_SENSOR_PIN, raw, CYD_AUTO_BACKLIGHT_BRIGHT_RAW,
      CYD_AUTO_BACKLIGHT_DARK_RAW, ambientBacklightPercent,
      CYD_AUTO_BACKLIGHT_MIN_PERCENT, CYD_AUTO_BACKLIGHT_MAX_PERCENT);
}

void updateAmbientBacklight() {
  if (!ambientBacklightReady) {
    return;
  }

  static unsigned long lastReadMs = 0;
  static unsigned long lastReportMs = 0;
  const unsigned long now = millis();
  if (now - lastReadMs < CYD_AUTO_BACKLIGHT_READ_INTERVAL_MS) {
    return;
  }
  lastReadMs = now;

  const uint16_t raw = readAmbientLightRaw();
  ambientRawEma =
      static_cast<uint16_t>((static_cast<uint32_t>(ambientRawEma) * 7 + raw +
                             4) /
                            8);
  const uint8_t targetPercent = mapAmbientRawToBacklight(ambientRawEma);
  const uint8_t nextPercent =
      stepBacklightToward(ambientBacklightPercent, targetPercent);
  if (nextPercent != ambientBacklightPercent) {
    ambientBacklightPercent = nextPercent;
    matrix.setBacklightPercent(ambientBacklightPercent);
  }

#if CYD_PERF_DEBUG
  if (now - lastReportMs >= CYD_AUTO_BACKLIGHT_REPORT_INTERVAL_MS) {
    lastReportMs = now;
    Serial.printf("auto_backlight: raw=%u ema=%u target=%u current=%u\n", raw,
                  ambientRawEma, targetPercent, ambientBacklightPercent);
  }
#endif
}
#else
void setupAmbientBacklight() {}
void updateAmbientBacklight() {}
#endif

void renderClockOverlay() {
#if CYD_SHOW_CLOCK
  if (matrix.foreground == nullptr || clockBaseEpoch <= 0) {
    return;
  }

  static const char* dayLabels[] = {"SUN", "MON", "TUE", "WED",
                                    "THU", "FRI", "SAT"};
  static const char* monthLabels[] = {"JAN", "FEB", "MAR", "APR",
                                      "MAY", "JUN", "JUL", "AUG",
                                      "SEP", "OCT", "NOV", "DEC"};

  const time_t now =
      clockBaseEpoch + static_cast<time_t>((millis() - clockBaseMillis) / 1000);
  tm currentTime = {};
  localtime_r(&now, &currentTime);

  char timeText[6] = {};
  char meridiemText[3] = {};
  char secondsText[3] = {};
  char dateText[12] = {};
  const bool isPm = currentTime.tm_hour >= 12;
  int hour12 = currentTime.tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  const char colonChar = (currentTime.tm_sec % 2) == 0 ? ':' : ' ';
  snprintf(timeText, sizeof(timeText), "%d%c%02d", hour12, colonChar,
           currentTime.tm_min);
  snprintf(meridiemText, sizeof(meridiemText), "%s", isPm ? "PM" : "AM");
  snprintf(secondsText, sizeof(secondsText), "%02d", currentTime.tm_sec);
  snprintf(dateText, sizeof(dateText), "%s %02d %s",
           dayLabels[currentTime.tm_wday], currentTime.tm_mday,
           monthLabels[currentTime.tm_mon]);

  const int16_t centerX = CydMatrixSettings::LOGICAL_WIDTH / 2;
  const uint16_t timeColor = matrix.foreground->color565(224, 248, 238);
  const uint16_t dateColor = matrix.foreground->color565(132, 222, 190);
  const uint16_t shadowColor = matrix.foreground->color565(0, 6, 8);

  const uint8_t timeScale = 2;
  const uint8_t timeSpacing = 1;
  const uint8_t suffixScale = 1;
  const uint8_t suffixSpacing = 1;
  const uint8_t suffixGap = 2;
  const uint16_t timeWidth =
      pixelTextWidth(timeText, timeScale, timeSpacing);
  const uint16_t suffixWidth =
      pixelTextWidth(meridiemText, suffixScale, suffixSpacing);
  const uint16_t totalWidth = timeWidth + suffixGap + suffixWidth;
  const int16_t timeX = centerX - static_cast<int16_t>(totalWidth / 2);
  const int16_t suffixX = timeX + timeWidth + suffixGap;
  const int16_t timeY = 3;
  const int16_t suffixY = timeY;
  const int16_t secondsY = suffixY + 6;

  drawPixelText(timeText, timeX + 1, timeY + 1, timeScale, timeSpacing,
                shadowColor);
  drawPixelText(meridiemText, suffixX + 1, suffixY + 1, suffixScale,
                suffixSpacing, shadowColor);
  drawPixelText(secondsText, suffixX + 1, secondsY + 1, suffixScale,
                suffixSpacing, shadowColor);
  drawPixelText(timeText, timeX, timeY, timeScale, timeSpacing, timeColor);
  drawPixelText(meridiemText, suffixX, suffixY, suffixScale, suffixSpacing,
                timeColor);
  drawPixelText(secondsText, suffixX, secondsY, suffixScale, suffixSpacing,
                dateColor);
  drawCenteredPixelText(dateText, centerX,
                        CydMatrixSettings::LOGICAL_HEIGHT - 8, 1, 1,
                        dateColor, shadowColor);
#endif  // CYD_SHOW_CLOCK
}

void forceFixedEnvironment() {
  State* state = stateManager.getState();
  state->environment.temperature.value = DEFAULT_TEMPERATURE_VALUE;
  state->environment.temperature.diff.type = DiffType::DISABLE;
  state->environment.temperature_fahrenheit.value =
      DEFAULT_TEMPERATURE_VALUE * 9.0f / 5.0f + 32.0f;
  state->environment.temperature_fahrenheit.diff.type = DiffType::DISABLE;
  state->environment.humidity.value = FIXED_HUMIDITY_PERCENT;
  state->environment.humidity.diff.type = DiffType::DISABLE;
  state->environment.co2.value = DEFAULT_CO2_VALUE;
  state->environment.co2.diff.type = DiffType::DISABLE;
}

void forcePreviewDisplayState() {
  State* state = stateManager.getState();
  state->autobrightness = false;
  state->brightness = CYD_PREVIEW_BRIGHTNESS;
}

void printBootDebug() {
#if CYD_TOUCH_DEBUG || CYD_PERF_DEBUG
  Serial.println();
  Serial.println("CYD Aquarium boot");
  Serial.printf("logical=%ux%u physical=%ux%u viewport=%u,%u %ux%u\n",
                CydMatrixSettings::LOGICAL_WIDTH,
                CydMatrixSettings::LOGICAL_HEIGHT,
                CydMatrixSettings::PHYSICAL_WIDTH,
                CydMatrixSettings::PHYSICAL_HEIGHT,
                CydMatrixSettings::VIEWPORT_X,
                CydMatrixSettings::VIEWPORT_Y,
                CydMatrixSettings::VIEWPORT_WIDTH,
                CydMatrixSettings::VIEWPORT_HEIGHT);
  Serial.printf("fixed env=%.1f C / %d ppm CO2 / %u%% RH\n",
                static_cast<double>(DEFAULT_TEMPERATURE_VALUE),
                static_cast<int>(DEFAULT_CO2_VALUE), FIXED_HUMIDITY_PERCENT);
  Serial.printf("touch pins mosi=%d miso=%d sclk=%d cs=%d irq=%d\n",
                CydMatrixSettings::TOUCH_MOSI_PIN,
                CydMatrixSettings::TOUCH_MISO_PIN,
                CydMatrixSettings::TOUCH_SCLK_PIN,
                CydMatrixSettings::TOUCH_CS_PIN,
                CydMatrixSettings::TOUCH_IRQ_PIN);
  Serial.printf("startup smoke test=%s\n",
                CYD_STARTUP_SMOKE_TEST ? "on" : "off");
  Serial.printf("framebuffer renderer=%s bytes=%lu dirty_rows=%s\n",
                CYD_FRAMEBUFFER_RENDERER ? "on" : "off",
                static_cast<unsigned long>(
                    CydMatrixSettings::VIEWPORT_WIDTH *
                    CydMatrixSettings::VIEWPORT_HEIGHT * sizeof(uint16_t)),
                CYD_FRAMEBUFFER_RENDERER ? "on" : "off");
  Serial.printf("dot renderer=%s radius_ratio=0.43\n",
                CYD_DOT_RENDERER ? "on" : "off");
  Serial.printf("autonomous life=%s food_interval=%lu-%lu max_food=%u\n",
                CYD_AUTONOMOUS_LIFE ? "on" : "off",
                static_cast<unsigned long>(CYD_AUTONOMOUS_FOOD_MIN_MS),
                static_cast<unsigned long>(CYD_AUTONOMOUS_FOOD_MAX_MS),
                CYD_AUTONOMOUS_MAX_FOOD);
  Serial.printf(
      "rich life curated_boot=%s rich_mix=%s keep_inside=%s fish_start=%u "
      "fish_ideal=%u\n",
      CYD_CURATED_BOOT_POPULATION ? "on" : "off",
      CYD_RICH_CREATURE_MIX ? "on" : "off",
      CYD_KEEP_CREATURES_ON_SCREEN ? "on" : "off", NUM_FISH_START,
      NUM_FISH_IDEAL);
  Serial.printf("perf debug=%s target_fps=%u frame_interval_ms=%lu\n",
                CYD_PERF_DEBUG ? "on" : "off", IDEAL_FPS,
                FRAME_INTERVAL_MS);
  Serial.printf("clock wifi_time=%s resync_ms=%lu retry_ms=%lu disconnect=%s\n",
                CYD_WIFI_TIME ? "on" : "off",
                static_cast<unsigned long>(CYD_NTP_RESYNC_INTERVAL_MS),
                static_cast<unsigned long>(CYD_NTP_RETRY_INTERVAL_MS),
                CYD_WIFI_DISCONNECT_AFTER_NTP ? "on" : "off");
  Serial.printf(
      "auto_backlight=%s pin=%u raw=%u-%u percent=%u-%u step=%u interval_ms=%lu\n",
      CYD_AUTO_BACKLIGHT ? "on" : "off", CYD_AUTO_BACKLIGHT_SENSOR_PIN,
      CYD_AUTO_BACKLIGHT_BRIGHT_RAW, CYD_AUTO_BACKLIGHT_DARK_RAW,
      CYD_AUTO_BACKLIGHT_MIN_PERCENT, CYD_AUTO_BACKLIGHT_MAX_PERCENT,
      CYD_AUTO_BACKLIGHT_STEP_PERCENT,
      static_cast<unsigned long>(CYD_AUTO_BACKLIGHT_READ_INTERVAL_MS));
#endif
}

void printTouchDebug(const char* event, const CydTouchPoint& touchPoint) {
#if CYD_TOUCH_DEBUG
  Serial.printf(
      "touch:%s pressed=%u raw=%u,%u screen=%u,%u logical=%u,%u\n", event,
      touchPoint.pressed ? 1 : 0, touchPoint.rawX, touchPoint.rawY,
      touchPoint.screenX, touchPoint.screenY, touchPoint.logicalX,
      touchPoint.logicalY);
#else
  (void)event;
  (void)touchPoint;
#endif
}

void recordPerfDebug(uint32_t frameDurationUs) {
#if CYD_PERF_DEBUG
  static unsigned long windowStartMs = 0;
  static uint32_t frameCount = 0;
  static uint64_t totalFrameUs = 0;
  static uint32_t minFrameUs = 0xFFFFFFFFUL;
  static uint32_t maxFrameUs = 0;

  const unsigned long nowMs = millis();
  if (windowStartMs == 0) {
    windowStartMs = nowMs;
  }

  frameCount++;
  totalFrameUs += frameDurationUs;
  minFrameUs = min(minFrameUs, frameDurationUs);
  maxFrameUs = max(maxFrameUs, frameDurationUs);

  const unsigned long windowMs = nowMs - windowStartMs;
  if (windowMs >= PERF_REPORT_INTERVAL_MS && frameCount > 0) {
    const double fps = static_cast<double>(frameCount) * 1000.0 /
                       static_cast<double>(windowMs);
    const double avgMs =
        static_cast<double>(totalFrameUs) / 1000.0 / frameCount;
    Serial.printf(
        "perf:fps=%.1f frames=%lu frame_ms_avg=%.2f min=%.2f max=%.2f "
        "free_heap=%lu\n",
        fps, static_cast<unsigned long>(frameCount), avgMs,
        static_cast<double>(minFrameUs) / 1000.0,
        static_cast<double>(maxFrameUs) / 1000.0,
        static_cast<unsigned long>(ESP.getFreeHeap()));

    windowStartMs = nowMs;
    frameCount = 0;
    totalFrameUs = 0;
    minFrameUs = 0xFFFFFFFFUL;
    maxFrameUs = 0;
  }
#else
  (void)frameDurationUs;
#endif
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  printBootDebug();
  randomSeed(esp_random());

  matrix.init();
#if CYD_TOUCH_DEBUG || CYD_PERF_DEBUG
  Serial.printf("framebuffer active=%s\n",
                matrix.isFrameBufferActive() ? "yes" : "no");
  Serial.printf("rowbuffer active=%s\n",
                matrix.isRowBufferActive() ? "yes" : "no");
#endif
#if CYD_STARTUP_SMOKE_TEST
  matrix.runStartupSmokeTest();
#endif
  touch.init();
  initClock();
  startClockSync("boot");
  setupLightSensorDiagnostic();

  if (!LittleFS.begin(true)) {
    log_e("LittleFS mount failed on CYD target");
  }

  stateManager.restore();
  forceFixedEnvironment();
  forcePreviewDisplayState();
  matrix.setBrightness(stateManager.getState()->brightness);
  setupAmbientBacklight();

  aquarium.begin();
}

void loop() {
  static unsigned long lastFrameTime = 0;
#if CYD_TOUCH_DEBUG
  static unsigned long lastTouchDebugTime = 0;
#endif
  const unsigned long now = millis();
  updateClockSync();
  updateAmbientBacklight();

  if (now - lastFrameTime >= FRAME_INTERVAL_MS) {
    lastFrameTime = now;
    const uint32_t frameStartUs = micros();
    const CydTouchPoint& touchPoint = touch.update();
    if (touch.pressedStarted()) {
      aquarium.onTouchStarted();
      printTouchDebug("down", touchPoint);
#if CYD_TOUCH_DEBUG
      lastTouchDebugTime = now;
#endif
    } else if (touch.pressedReleased()) {
      aquarium.onTouchReleased();
      printTouchDebug("up", touchPoint);
    }

#if CYD_TOUCH_DEBUG
    if (touch.isPressed() && now - lastTouchDebugTime >= 500) {
      printTouchDebug("hold", touchPoint);
      lastTouchDebugTime = now;
    }
#endif

    forceFixedEnvironment();
    aquarium.update(false);
    renderClockOverlay();
    aquarium.display();
    matrix.update();
    recordPerfDebug(micros() - frameStartUs);
    updateLightSensorDiagnostic();
  }

  delay(1);
}

#endif  // CYD_TFT_DIAGNOSTIC

#endif  // PANEL_CYD_TFT
