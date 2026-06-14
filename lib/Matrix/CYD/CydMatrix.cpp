#ifdef PANEL_CYD_TFT

#include "CydMatrix.h"

#include <esp_heap_caps.h>

#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#define CYD_LEDC_V3 1
#else
#define CYD_LEDC_V3 0
#endif

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

#ifndef CYD_TFT_ROTATION
#define CYD_TFT_ROTATION 0
#endif

#ifndef CYD_TFT_BACKLIGHT_PWM
#define CYD_TFT_BACKLIGHT_PWM 1
#endif

#ifndef CYD_TFT_BACKLIGHT_PWM_FREQ
#define CYD_TFT_BACKLIGHT_PWM_FREQ 5000
#endif

#ifndef CYD_TFT_BACKLIGHT_PWM_RESOLUTION
#define CYD_TFT_BACKLIGHT_PWM_RESOLUTION 8
#endif

#ifndef CYD_TFT_BACKLIGHT_PWM_CHANNEL
#define CYD_TFT_BACKLIGHT_PWM_CHANNEL 0
#endif

#ifndef CYD_TFT_SWAP_BYTES
#define CYD_TFT_SWAP_BYTES 1
#endif

// Some CYD panel revisions use BGR color order, which makes blue render as
// orange/red. Setting this swaps the red and blue channels at output.
#ifndef CYD_TFT_SWAP_RB
#define CYD_TFT_SWAP_RB 0
#endif

#ifndef CYD_TFT_TONE_CORRECTION
#define CYD_TFT_TONE_CORRECTION 1
#endif

#ifndef CYD_TFT_TONE_CURVE_STRENGTH
#define CYD_TFT_TONE_CURVE_STRENGTH 220
#endif

#ifndef CYD_TFT_TONE_BLACK_THRESHOLD
#define CYD_TFT_TONE_BLACK_THRESHOLD 4
#endif

#ifndef CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH
#define CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH CYD_TFT_TONE_CURVE_STRENGTH
#endif

#ifndef CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH
#define CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH 0
#endif

#ifndef CYD_TFT_FOREGROUND_GAIN
#define CYD_TFT_FOREGROUND_GAIN 170
#endif

#ifndef CYD_TFT_FOREGROUND_SATURATION
#define CYD_TFT_FOREGROUND_SATURATION 190
#endif

#ifndef CYD_BACKGROUND_BRIGHTNESS
#define CYD_BACKGROUND_BRIGHTNESS 44
#endif

#ifndef CYD_BACKGROUND_BLACK_THRESHOLD
#define CYD_BACKGROUND_BLACK_THRESHOLD 9
#endif

#ifndef CYD_FOREGROUND_BRIGHTNESS
#define CYD_FOREGROUND_BRIGHTNESS 235
#endif

#ifndef CYD_FOREGROUND_SATURATION
#define CYD_FOREGROUND_SATURATION 160
#endif

namespace {

constexpr uint8_t kColorProfileBackground = 1;
constexpr uint8_t kColorProfileForeground = 2;

constexpr uint8_t backlightOffLevel() {
  return TFT_BACKLIGHT_ON == HIGH ? LOW : HIGH;
}

constexpr uint32_t frameBufferPixelCount() {
  return static_cast<uint32_t>(CydMatrixSettings::VIEWPORT_WIDTH) *
         CydMatrixSettings::VIEWPORT_HEIGHT;
}

constexpr size_t frameBufferByteCount() {
  return frameBufferPixelCount() * sizeof(uint16_t);
}

constexpr uint16_t rowBufferLogicalHeight() {
  return 4;
}

constexpr uint16_t maxRowBufferHeight() {
  uint16_t maxHeight = 1;
  for (uint16_t y = 0; y < CydMatrixSettings::LOGICAL_HEIGHT;
       y += rowBufferLogicalHeight()) {
    const uint16_t y0 = CydMatrixSettings::screenYForLogicalEdge(y);
    const uint16_t nextYCandidate = y + rowBufferLogicalHeight();
    const uint16_t nextY =
        nextYCandidate < CydMatrixSettings::LOGICAL_HEIGHT
            ? nextYCandidate
            : CydMatrixSettings::LOGICAL_HEIGHT;
    const uint16_t y1 = CydMatrixSettings::screenYForLogicalEdge(nextY);
    const uint16_t height = y1 > y0 ? y1 - y0 : 1;
    maxHeight = height > maxHeight ? height : maxHeight;
  }
  return maxHeight;
}

constexpr size_t rowBufferByteCount() {
  return static_cast<size_t>(CydMatrixSettings::VIEWPORT_WIDTH) *
         maxRowBufferHeight() * sizeof(uint16_t);
}

constexpr float dotRadiusRatio() {
  return 0.43f;
}

uint8_t scaleChannelByBrightness(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(value) * brightness + 127) / 255);
}

uint8_t applyTftToneCurve(uint8_t value, uint16_t strength,
                          uint8_t blackThreshold) {
#if CYD_TFT_TONE_CORRECTION
  if (value == 0 || value == 255) {
    return value;
  }

  strength = strength > 255 ? 255 : strength;
  const uint16_t linearWeight = 255 - strength;
  const uint16_t quadratic =
      (static_cast<uint16_t>(value) * value + 127) / 255;
  const uint16_t corrected =
      (static_cast<uint16_t>(value) * linearWeight + quadratic * strength +
       127) /
      255;

  if (blackThreshold > 0 && corrected < blackThreshold) {
    return 0;
  }
  return static_cast<uint8_t>(corrected > 255 ? 255 : corrected);
#else
  return value;
#endif
}

uint8_t maxChannel(uint8_t r, uint8_t g, uint8_t b) {
  return max(r, max(g, b));
}

uint8_t luma8(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(r) * 77 + static_cast<uint16_t>(g) * 150 +
       static_cast<uint16_t>(b) * 29) >>
      8);
}

uint8_t saturateChannel(uint8_t value, uint8_t luma, uint16_t saturation) {
  const int16_t delta = static_cast<int16_t>(value) - luma;
  const int16_t saturated =
      static_cast<int16_t>(luma) + ((delta * saturation) / 128);
  return static_cast<uint8_t>(constrain(saturated, 0, 255));
}

uint8_t gainChannel(uint8_t value, uint16_t gain) {
  return static_cast<uint8_t>(
      constrain((static_cast<uint16_t>(value) * gain + 64) / 128, 0, 255));
}

CRGB applyTftForegroundProfile(CRGB color) {
  if (color.r == 0 && color.g == 0 && color.b == 0) {
    return color;
  }

  const uint8_t luma = luma8(color.r, color.g, color.b);
  color.r = saturateChannel(color.r, luma, CYD_TFT_FOREGROUND_SATURATION);
  color.g = saturateChannel(color.g, luma, CYD_TFT_FOREGROUND_SATURATION);
  color.b = saturateChannel(color.b, luma, CYD_TFT_FOREGROUND_SATURATION);

  color.r = gainChannel(color.r, CYD_TFT_FOREGROUND_GAIN);
  color.g = gainChannel(color.g, CYD_TFT_FOREGROUND_GAIN);
  color.b = gainChannel(color.b, CYD_TFT_FOREGROUND_GAIN);
  return color;
}

CRGB applyDisplayProfile(CRGB color, uint8_t brightness,
                         uint16_t saturation = 128,
                         uint8_t blackThreshold = 0) {
  if (color.r == 0 && color.g == 0 && color.b == 0) {
    return CRGB(0, 0, 0);
  }

  if (saturation != 128) {
    const uint8_t luma = luma8(color.r, color.g, color.b);
    color.r = saturateChannel(color.r, luma, saturation);
    color.g = saturateChannel(color.g, luma, saturation);
    color.b = saturateChannel(color.b, luma, saturation);
  }

  color.r = scaleChannelByBrightness(color.r, brightness);
  color.g = scaleChannelByBrightness(color.g, brightness);
  color.b = scaleChannelByBrightness(color.b, brightness);

  if (blackThreshold > 0 &&
      maxChannel(color.r, color.g, color.b) < blackThreshold) {
    return CRGB(0, 0, 0);
  }

  return color;
}

}  // namespace

CydMatrix::CydMatrix()
    : tft(TFT_eSPI()),
      frameBuffer(nullptr),
      rowBuffer(nullptr),
      previousLogicalPixels(nullptr),
      rowBufferLogicalY(-1),
      rowBufferY0(0),
      rowBufferY1(0),
      rowBufferDirty(true),
      dirtyRowStart(CydMatrixSettings::VIEWPORT_HEIGHT),
      dirtyRowEnd(0),
      backlightPwmAttached(false),
      backlightPercent(100) {
  fontSize = 2;
  rotation = CYD_TFT_ROTATION;
}

void CydMatrix::init() {
  allocatePreviousLogicalPixels();
  allocateFrameBuffer();
  tft.init();
  tft.setRotation(rotation);
  tft.setSwapBytes(CYD_TFT_SWAP_BYTES != 0);
  Serial.printf("CYD TFT rotation=%u\n", rotation);
  Serial.printf("CYD TFT pushImage swapBytes=%s\n",
                tft.getSwapBytes() ? "on" : "off");
  Serial.printf("CYD TFT tone correction=%s strength=%u black_threshold=%u\n",
                CYD_TFT_TONE_CORRECTION ? "on" : "off",
                CYD_TFT_TONE_CURVE_STRENGTH,
                CYD_TFT_TONE_BLACK_THRESHOLD);
  Serial.printf("CYD TFT background profile tone=%u brightness=%u black_threshold=%u\n",
                CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH,
                CYD_BACKGROUND_BRIGHTNESS, CYD_BACKGROUND_BLACK_THRESHOLD);
  Serial.printf(
      "CYD TFT foreground profile tone=%u gain=%u saturation=%u\n",
      CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH, CYD_TFT_FOREGROUND_GAIN,
      CYD_TFT_FOREGROUND_SATURATION);
  tft.fillScreen(TFT_BLACK);
  initBacklightPwm();
  setBacklightEnabled(brightness > 0);

  background = new GFX_Layer(
      CydMatrixSettings::LOGICAL_WIDTH, CydMatrixSettings::LOGICAL_HEIGHT,
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });

  foreground = new GFX_Layer(
      CydMatrixSettings::LOGICAL_WIDTH, CydMatrixSettings::LOGICAL_HEIGHT,
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });

  gfx_compositor = new GFX_LayerCompositor(
      [this](int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
        drawPixelRGB888(x, y, r, g, b);
      });
}

uint16_t CydMatrix::packRgb565ForDisplay(uint8_t r, uint8_t g, uint8_t b,
                                         uint8_t brightnessScale,
                                         uint8_t colorProfile) {
  CRGB color(scaleChannelByBrightness(r, brightnessScale),
             scaleChannelByBrightness(g, brightnessScale),
             scaleChannelByBrightness(b, brightnessScale));

  if (colorProfile == kColorProfileForeground) {
    color = applyTftForegroundProfile(color);
    color.r = applyTftToneCurve(color.r, CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH,
                                0);
    color.g = applyTftToneCurve(color.g, CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH,
                                0);
    color.b = applyTftToneCurve(color.b, CYD_TFT_FOREGROUND_TONE_CURVE_STRENGTH,
                                0);
  } else {
    color.r = applyTftToneCurve(color.r, CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH,
                                CYD_TFT_TONE_BLACK_THRESHOLD);
    color.g = applyTftToneCurve(color.g, CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH,
                                CYD_TFT_TONE_BLACK_THRESHOLD);
    color.b = applyTftToneCurve(color.b, CYD_TFT_BACKGROUND_TONE_CURVE_STRENGTH,
                                CYD_TFT_TONE_BLACK_THRESHOLD);
  }

#if CYD_TFT_SWAP_RB
  return tft.color565(color.b, color.g, color.r);
#else
  return tft.color565(color.r, color.g, color.b);
#endif
}

uint16_t CydMatrix::toRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return packRgb565ForDisplay(r, g, b, brightness, kColorProfileBackground);
}

void CydMatrix::allocateFrameBuffer() {
#if CYD_FRAMEBUFFER_RENDERER
  frameBuffer = static_cast<uint16_t*>(
      heap_caps_malloc(frameBufferByteCount(), MALLOC_CAP_8BIT));

  if (frameBuffer == nullptr) {
    Serial.printf(
        "CYD framebuffer allocation failed bytes=%lu largest_free=%lu; "
        "fallback=rowBuffer\n",
        static_cast<unsigned long>(frameBufferByteCount()),
        static_cast<unsigned long>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    allocateRowBuffer();
    return;
  }

  clearFrameBuffer(TFT_BLACK);
#else
  allocateRowBuffer();
#endif
}

void CydMatrix::allocateRowBuffer() {
  rowBuffer =
      static_cast<uint16_t*>(heap_caps_malloc(rowBufferByteCount(), MALLOC_CAP_8BIT));

  if (rowBuffer == nullptr) {
    Serial.printf(
        "CYD row buffer allocation failed bytes=%lu largest_free=%lu; "
        "fallback=directDraw\n",
        static_cast<unsigned long>(rowBufferByteCount()),
        static_cast<unsigned long>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    return;
  }

  Serial.printf("CYD tile buffer active bytes=%lu logical_rows=%u max_height=%u\n",
                static_cast<unsigned long>(rowBufferByteCount()),
                rowBufferLogicalHeight(),
                maxRowBufferHeight());
}

void CydMatrix::allocatePreviousLogicalPixels() {
  const size_t bytes = static_cast<size_t>(CydMatrixSettings::LOGICAL_WIDTH) *
                       CydMatrixSettings::LOGICAL_HEIGHT * sizeof(uint16_t);
  previousLogicalPixels =
      static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));

  if (previousLogicalPixels == nullptr) {
    Serial.printf(
        "CYD dirty tile allocation failed bytes=%lu largest_free=%lu; "
        "fallback=push_all_tiles\n",
        static_cast<unsigned long>(bytes),
        static_cast<unsigned long>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    return;
  }

  for (uint32_t i = 0;
       i < static_cast<uint32_t>(CydMatrixSettings::LOGICAL_WIDTH) *
               CydMatrixSettings::LOGICAL_HEIGHT;
       ++i) {
    previousLogicalPixels[i] = 0xFFFF;
  }

  Serial.printf("CYD dirty tile tracker active bytes=%lu\n",
                static_cast<unsigned long>(bytes));
}

void CydMatrix::clearFrameBuffer(uint16_t color) {
  if (frameBuffer == nullptr) {
    if (rowBuffer != nullptr) {
      rowBufferLogicalY = -1;
      rowBufferY0 = 0;
      rowBufferY1 = 0;
      rowBufferDirty = true;
    }
    if (previousLogicalPixels != nullptr) {
      for (uint32_t i = 0;
           i < static_cast<uint32_t>(CydMatrixSettings::LOGICAL_WIDTH) *
                   CydMatrixSettings::LOGICAL_HEIGHT;
           ++i) {
        previousLogicalPixels[i] = 0xFFFF;
      }
    }
    return;
  }

  for (uint32_t i = 0; i < frameBufferPixelCount(); ++i) {
    frameBuffer[i] = color;
  }
  markFrameBufferRowsDirty(0, CydMatrixSettings::VIEWPORT_HEIGHT);
}

void CydMatrix::markFrameBufferRowsDirty(uint16_t rowStart, uint16_t rowEnd) {
  if (frameBuffer == nullptr || rowStart >= rowEnd) {
    return;
  }

  dirtyRowStart = min(dirtyRowStart, rowStart);
  dirtyRowEnd = max(dirtyRowEnd, rowEnd);
}

void CydMatrix::pushFrameBuffer() {
  flushRowBuffer();

  if (frameBuffer == nullptr || dirtyRowStart >= dirtyRowEnd) {
    return;
  }

  const uint16_t dirtyHeight = dirtyRowEnd - dirtyRowStart;
  uint16_t* dirtyRows =
      frameBuffer +
      static_cast<uint32_t>(dirtyRowStart) * CydMatrixSettings::VIEWPORT_WIDTH;

  tft.pushImage(CydMatrixSettings::VIEWPORT_X,
                CydMatrixSettings::VIEWPORT_Y + dirtyRowStart,
                CydMatrixSettings::VIEWPORT_WIDTH, dirtyHeight, dirtyRows);

  dirtyRowStart = CydMatrixSettings::VIEWPORT_HEIGHT;
  dirtyRowEnd = 0;
}

void CydMatrix::resetRowBuffer(uint16_t logicalY) {
  const uint16_t tileStart =
      (logicalY / rowBufferLogicalHeight()) * rowBufferLogicalHeight();
  const uint16_t tileEndCandidate = tileStart + rowBufferLogicalHeight();
  const uint16_t tileEnd =
      tileEndCandidate < CydMatrixSettings::LOGICAL_HEIGHT
          ? tileEndCandidate
          : CydMatrixSettings::LOGICAL_HEIGHT;

  rowBufferLogicalY = tileStart;
  rowBufferDirty = previousLogicalPixels == nullptr;
  rowBufferY0 = CydMatrixSettings::screenYForLogicalEdge(tileStart);
  rowBufferY1 = CydMatrixSettings::screenYForLogicalEdge(tileEnd);
  if (rowBufferY1 <= rowBufferY0) {
    rowBufferY1 = rowBufferY0 + 1;
  }

  const uint16_t rowHeight = rowBufferY1 - rowBufferY0;
  for (uint32_t i = 0;
       i < static_cast<uint32_t>(CydMatrixSettings::VIEWPORT_WIDTH) * rowHeight;
       ++i) {
    rowBuffer[i] = TFT_BLACK;
  }
}

void CydMatrix::flushRowBuffer() {
  if (rowBuffer == nullptr || rowBufferLogicalY < 0) {
    return;
  }

  if (rowBufferDirty) {
    const uint16_t rowHeight = rowBufferY1 - rowBufferY0;
    tft.pushImage(CydMatrixSettings::VIEWPORT_X, rowBufferY0,
                  CydMatrixSettings::VIEWPORT_WIDTH, rowHeight, rowBuffer);
  }
  rowBufferLogicalY = -1;
}

void CydMatrix::writeRowBufferPixel(uint16_t x, uint16_t y, uint16_t color) {
  if (rowBuffer == nullptr) {
    return;
  }

  if (rowBufferLogicalY < 0 ||
      y < static_cast<uint16_t>(rowBufferLogicalY) ||
      y >= static_cast<uint16_t>(rowBufferLogicalY) + rowBufferLogicalHeight()) {
    flushRowBuffer();
    resetRowBuffer(y);
  }

  if (previousLogicalPixels != nullptr) {
    const uint32_t logicalIndex =
        static_cast<uint32_t>(y) * CydMatrixSettings::LOGICAL_WIDTH + x;
    if (previousLogicalPixels[logicalIndex] != color) {
      previousLogicalPixels[logicalIndex] = color;
      rowBufferDirty = true;
    }
  }

  const uint16_t x0 =
      CydMatrixSettings::screenXForLogicalEdge(x) - CydMatrixSettings::VIEWPORT_X;
  uint16_t x1 =
      CydMatrixSettings::screenXForLogicalEdge(x + 1) - CydMatrixSettings::VIEWPORT_X;
  if (x1 <= x0) {
    x1 = x0 + 1;
  }

  const uint16_t y0 =
      CydMatrixSettings::screenYForLogicalEdge(y) - rowBufferY0;
  uint16_t y1 =
      CydMatrixSettings::screenYForLogicalEdge(y + 1) - rowBufferY0;
  if (y1 <= y0) {
    y1 = y0 + 1;
  }
  const uint16_t cellHeight = y1 - y0;
#if CYD_DOT_RENDERER
  if (color == TFT_BLACK) {
    return;
  }

  const uint16_t width = x1 > x0 ? x1 - x0 : 1;
  const uint16_t height = cellHeight > 0 ? cellHeight : 1;
  if (width == 3 && height == 3) {
    const uint16_t centerX = x0 + 1;
    const uint16_t centerY = y0 + 1;
    uint16_t* centerRow =
        rowBuffer + static_cast<uint32_t>(centerY) *
                        CydMatrixSettings::VIEWPORT_WIDTH;
    uint16_t* upperRow =
        rowBuffer + static_cast<uint32_t>(centerY - 1) *
                        CydMatrixSettings::VIEWPORT_WIDTH;
    uint16_t* lowerRow =
        rowBuffer + static_cast<uint32_t>(centerY + 1) *
                        CydMatrixSettings::VIEWPORT_WIDTH;

    upperRow[centerX] = color;
    centerRow[centerX - 1] = color;
    centerRow[centerX] = color;
    centerRow[centerX + 1] = color;
    lowerRow[centerX] = color;
    return;
  }

  const uint16_t minDimension = width < height ? width : height;
  const float radius = max(0.75f, minDimension * dotRadiusRatio());
  const float radiusSq = radius * radius;
  const float centerX = (static_cast<float>(x0) + x1 - 1) * 0.5f;
  const float centerY = (static_cast<float>(y0) + y1 - 1) * 0.5f;

  for (uint16_t py = y0; py < y1; ++py) {
    uint16_t* row =
        rowBuffer + static_cast<uint32_t>(py) * CydMatrixSettings::VIEWPORT_WIDTH;
    for (uint16_t px = x0; px < x1; ++px) {
      const float dx = static_cast<float>(px) - centerX;
      const float dy = static_cast<float>(py) - centerY;
      if (dx * dx + dy * dy <= radiusSq) {
        row[px] = color;
      }
    }
  }
#else
  for (uint16_t py = y0; py < y1; ++py) {
    uint16_t* row =
        rowBuffer + static_cast<uint32_t>(py) * CydMatrixSettings::VIEWPORT_WIDTH;
    for (uint16_t px = x0; px < x1; ++px) {
      row[px] = color;
    }
  }
#endif
}

void CydMatrix::writeFrameBufferDot(uint16_t x0, uint16_t x1, uint16_t y0,
                                    uint16_t y1, uint16_t color) {
  if (frameBuffer == nullptr) {
    return;
  }

  const uint16_t bufferX0 = x0 - CydMatrixSettings::VIEWPORT_X;
  const uint16_t bufferX1 = x1 - CydMatrixSettings::VIEWPORT_X;
  const uint16_t bufferY0 = y0 - CydMatrixSettings::VIEWPORT_Y;
  const uint16_t bufferY1 = y1 - CydMatrixSettings::VIEWPORT_Y;
  const uint16_t width = bufferX1 > bufferX0 ? bufferX1 - bufferX0 : 1;
  const uint16_t height = bufferY1 > bufferY0 ? bufferY1 - bufferY0 : 1;
  const uint16_t minDimension = width < height ? width : height;
  const float radius = max(0.75f, minDimension * dotRadiusRatio());
  const float radiusSq = radius * radius;
  const float centerX = (static_cast<float>(bufferX0) + bufferX1 - 1) * 0.5f;
  const float centerY = (static_cast<float>(bufferY0) + bufferY1 - 1) * 0.5f;

  for (uint16_t py = bufferY0; py < bufferY1; ++py) {
    uint16_t* row =
        frameBuffer +
        static_cast<uint32_t>(py) * CydMatrixSettings::VIEWPORT_WIDTH;
    for (uint16_t px = bufferX0; px < bufferX1; ++px) {
      const float dx = static_cast<float>(px) - centerX;
      const float dy = static_cast<float>(py) - centerY;
      if (dx * dx + dy * dy <= radiusSq) {
        row[px] = color;
      }
    }
  }

  markFrameBufferRowsDirty(bufferY0, bufferY1);
}

void CydMatrix::writeScaledLogicalPixel(uint16_t x, uint16_t y,
                                        uint16_t color) {
  if (x >= CydMatrixSettings::LOGICAL_WIDTH ||
      y >= CydMatrixSettings::LOGICAL_HEIGHT) {
    return;
  }

  const uint16_t x0 = CydMatrixSettings::screenXForLogicalEdge(x);
  const uint16_t x1 = CydMatrixSettings::screenXForLogicalEdge(x + 1);
  const uint16_t y0 = CydMatrixSettings::screenYForLogicalEdge(y);
  const uint16_t y1 = CydMatrixSettings::screenYForLogicalEdge(y + 1);
  const uint16_t width = x1 > x0 ? x1 - x0 : 1;
  const uint16_t height = y1 > y0 ? y1 - y0 : 1;

  if (frameBuffer != nullptr) {
#if CYD_DOT_RENDERER
    writeFrameBufferDot(x0, x1, y0, y1, color);
    return;
#else
    const uint16_t bufferX0 = x0 - CydMatrixSettings::VIEWPORT_X;
    const uint16_t bufferX1 = x1 - CydMatrixSettings::VIEWPORT_X;
    const uint16_t bufferY0 = y0 - CydMatrixSettings::VIEWPORT_Y;
    const uint16_t bufferY1 = y1 - CydMatrixSettings::VIEWPORT_Y;

    for (uint16_t py = bufferY0; py < bufferY1; ++py) {
      uint16_t* row =
          frameBuffer + static_cast<uint32_t>(py) * CydMatrixSettings::VIEWPORT_WIDTH;
      for (uint16_t px = bufferX0; px < bufferX1; ++px) {
        row[px] = color;
      }
    }
    markFrameBufferRowsDirty(bufferY0, bufferY1);
    return;
#endif
  }

  if (rowBuffer != nullptr) {
    writeRowBufferPixel(x, y, color);
    return;
  }

  // The dot fallback issues thousands of small circle draw calls and is too
  // slow on ESP32-2432S028R boards without enough contiguous RAM for the
  // framebuffer renderer. Keep the direct path rectangular so the first
  // hardware build remains animated and usable.
  tft.fillRect(x0, y0, width, height, color);
}

void CydMatrix::drawPixelRGB888(uint16_t x, uint16_t y, uint8_t r_data,
                                uint8_t g_data, uint8_t b_data) {
  writeScaledLogicalPixel(x, y, toRgb565(r_data, g_data, b_data));
}

void CydMatrix::drawPixelRGB888Profile(uint16_t x, uint16_t y, uint8_t r_data,
                                       uint8_t g_data, uint8_t b_data,
                                       bool foregroundProfile) {
  writeScaledLogicalPixel(
      x, y,
      packRgb565ForDisplay(r_data, g_data, b_data, brightness,
                           foregroundProfile ? kColorProfileForeground
                                             : kColorProfileBackground));
}

void CydMatrix::setBrightness(uint8_t newBrightness) {
  brightness = newBrightness;
  Serial.printf("CYD color brightness=%u/255\n", brightness);
  setBacklightEnabled(brightness > 0);
}

uint8_t CydMatrix::getBrightness() const {
  return brightness;
}

uint8_t CydMatrix::getXResolution() {
  return CydMatrixSettings::LOGICAL_WIDTH;
}

uint8_t CydMatrix::getYResolution() {
  return CydMatrixSettings::LOGICAL_HEIGHT;
}

void CydMatrix::setRotation(uint8_t newRotation) {
  if (newRotation < 4 && newRotation != rotation) {
    rotation = newRotation;
    tft.setRotation(rotation);
  }
}

void CydMatrix::rotate90() {
  rotation = (rotation + 1) % 4;
  tft.setRotation(rotation);
}

void CydMatrix::clearScreen() {
  clearFrameBuffer(TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
}

void CydMatrix::compositeLayers() {
  if (background == nullptr || foreground == nullptr) {
    return;
  }

  for (uint16_t y = 0; y < CydMatrixSettings::LOGICAL_HEIGHT; ++y) {
    for (uint16_t x = 0; x < CydMatrixSettings::LOGICAL_WIDTH; ++x) {
      const CRGB foregroundPixel = foreground->pixels->data[y][x];
      const bool hasForeground =
          foregroundPixel != foreground->transparency_colour;
      const CRGB source =
          hasForeground ? foregroundPixel : background->pixels->data[y][x];
      const CRGB corrected =
          hasForeground
              ? applyDisplayProfile(source, CYD_FOREGROUND_BRIGHTNESS,
                                    CYD_FOREGROUND_SATURATION, 0)
              : applyDisplayProfile(source, CYD_BACKGROUND_BRIGHTNESS, 128,
                                    CYD_BACKGROUND_BLACK_THRESHOLD);

      writeScaledLogicalPixel(
          x, y,
          packRgb565ForDisplay(corrected.r, corrected.g, corrected.b, 255,
                               hasForeground ? kColorProfileForeground
                                             : kColorProfileBackground));
    }
  }

  foreground->clear();
}

bool CydMatrix::isFrameBufferActive() const {
  return frameBuffer != nullptr;
}

bool CydMatrix::isRowBufferActive() const {
  return rowBuffer != nullptr;
}

void CydMatrix::runStartupSmokeTest() {
  setBacklightEnabled(true);

  const uint16_t fillColors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_BLACK};
  for (uint16_t color : fillColors) {
    tft.fillScreen(color);
    delay(180);
  }

  tft.fillScreen(TFT_BLACK);

  const uint16_t darkWater = toRgb565(3, 16, 30);
  const uint16_t lightWater = toRgb565(7, 32, 50);
  for (uint16_t y = 0; y < CydMatrixSettings::LOGICAL_HEIGHT; ++y) {
    for (uint16_t x = 0; x < CydMatrixSettings::LOGICAL_WIDTH; ++x) {
      const bool checker = ((x / 4) + (y / 4)) % 2 == 0;
      writeScaledLogicalPixel(x, y, checker ? darkWater : lightWater);
    }
  }
  pushFrameBuffer();

  tft.drawRect(CydMatrixSettings::VIEWPORT_X, CydMatrixSettings::VIEWPORT_Y,
               CydMatrixSettings::VIEWPORT_WIDTH,
               CydMatrixSettings::VIEWPORT_HEIGHT, TFT_WHITE);

  const uint16_t cornerSize = 8;
  tft.fillRect(CydMatrixSettings::VIEWPORT_X, CydMatrixSettings::VIEWPORT_Y,
               cornerSize, cornerSize, TFT_RED);
  tft.fillRect(CydMatrixSettings::VIEWPORT_X + CydMatrixSettings::VIEWPORT_WIDTH -
                   cornerSize,
               CydMatrixSettings::VIEWPORT_Y, cornerSize, cornerSize,
               TFT_GREEN);
  tft.fillRect(CydMatrixSettings::VIEWPORT_X,
               CydMatrixSettings::VIEWPORT_Y +
                   CydMatrixSettings::VIEWPORT_HEIGHT - cornerSize,
               cornerSize, cornerSize, TFT_BLUE);
  tft.fillRect(CydMatrixSettings::VIEWPORT_X + CydMatrixSettings::VIEWPORT_WIDTH -
                   cornerSize,
               CydMatrixSettings::VIEWPORT_Y +
                   CydMatrixSettings::VIEWPORT_HEIGHT - cornerSize,
               cornerSize, cornerSize, TFT_YELLOW);

  delay(1200);
  clearFrameBuffer(TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
}

void CydMatrix::update() {
  pushFrameBuffer();
}

void CydMatrix::initBacklightPwm() {
  if (CydMatrixSettings::TFT_BL_PIN < 0) {
    return;
  }

  pinMode(CydMatrixSettings::TFT_BL_PIN, OUTPUT);
#if CYD_TFT_BACKLIGHT_PWM
#if CYD_LEDC_V3
  backlightPwmAttached =
      ledcAttach(CydMatrixSettings::TFT_BL_PIN, CYD_TFT_BACKLIGHT_PWM_FREQ,
                 CYD_TFT_BACKLIGHT_PWM_RESOLUTION);
#else
  ledcSetup(CYD_TFT_BACKLIGHT_PWM_CHANNEL, CYD_TFT_BACKLIGHT_PWM_FREQ,
            CYD_TFT_BACKLIGHT_PWM_RESOLUTION);
  ledcAttachPin(CydMatrixSettings::TFT_BL_PIN, CYD_TFT_BACKLIGHT_PWM_CHANNEL);
  backlightPwmAttached = true;
#endif
  Serial.printf("CYD TFT backlight pwm=%s pin=%d freq=%lu resolution=%u\n",
                backlightPwmAttached ? "on" : "failed",
                CydMatrixSettings::TFT_BL_PIN,
                static_cast<unsigned long>(CYD_TFT_BACKLIGHT_PWM_FREQ),
                CYD_TFT_BACKLIGHT_PWM_RESOLUTION);
#else
  backlightPwmAttached = false;
  Serial.printf("CYD TFT backlight pwm=off pin=%d\n",
                CydMatrixSettings::TFT_BL_PIN);
#endif
}

void CydMatrix::writeBacklightPercent(uint8_t percent) {
  if (CydMatrixSettings::TFT_BL_PIN < 0) {
    return;
  }

  percent = constrain(percent, 0, 100);
  if (backlightPwmAttached) {
    const uint32_t dutyMax =
        (1UL << CYD_TFT_BACKLIGHT_PWM_RESOLUTION) - 1UL;
    const uint32_t onDuty =
        (static_cast<uint32_t>(percent) * dutyMax + 50UL) / 100UL;
    const uint32_t outputDuty =
        TFT_BACKLIGHT_ON == HIGH ? onDuty : dutyMax - onDuty;
#if CYD_LEDC_V3
    ledcWrite(CydMatrixSettings::TFT_BL_PIN, outputDuty);
#else
    ledcWrite(CYD_TFT_BACKLIGHT_PWM_CHANNEL, outputDuty);
#endif
    return;
  }

  digitalWrite(CydMatrixSettings::TFT_BL_PIN,
               percent > 0 ? TFT_BACKLIGHT_ON : backlightOffLevel());
}

void CydMatrix::setBacklightEnabled(bool enabled) {
  if (CydMatrixSettings::TFT_BL_PIN < 0) {
    return;
  }

  writeBacklightPercent(enabled ? backlightPercent : 0);
}

void CydMatrix::setBacklightPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  if (backlightPercent == percent) {
    return;
  }

  backlightPercent = percent;
  if (brightness > 0) {
    writeBacklightPercent(backlightPercent);
  }
}

uint8_t CydMatrix::getBacklightPercent() const {
  return backlightPercent;
}

void CydMatrix::setColorDiagnostic(bool invert, bool swap) {
  tft.invertDisplay(invert);
  tft.setSwapBytes(swap);
}

// Draws seven color rows (top to bottom: RED, GREEN, BLUE, YELLOW, CYAN,
// MAGENTA, WHITE). Each row has three columns so the correct byte-swap setting
// for the aquarium's pushImage path can be read by eye:
//   col 0 (left)   = fillRect reference (always correct on this panel)
//   col 1 (middle) = pushImage with byte-swap ON
//   col 2 (right)  = pushImage with byte-swap OFF
// Whichever pushImage column matches the left reference is the right setting.
void CydMatrix::drawColorTestCard() {
  struct TestBar {
    uint8_t r;
    uint8_t g;
    uint8_t b;
  };
  static const TestBar bars[] = {
      {255, 0, 0},   {0, 255, 0},   {0, 0, 255}, {255, 255, 0},
      {0, 255, 255}, {255, 0, 255}, {255, 255, 255},
  };
  const uint8_t count = sizeof(bars) / sizeof(bars[0]);
  const int16_t w = tft.width();
  const int16_t h = tft.height();
  const int16_t bandHeight = h / count;
  const int16_t maxBandHeight = h - (count - 1) * bandHeight;
  const int16_t colWidth = w / 3;

  const size_t bufPixels =
      static_cast<size_t>(colWidth) * static_cast<size_t>(maxBandHeight);
  uint16_t* buf = static_cast<uint16_t*>(malloc(bufPixels * sizeof(uint16_t)));

  tft.fillScreen(TFT_BLACK);
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t color = tft.color565(bars[i].r, bars[i].g, bars[i].b);
    const int16_t y = i * bandHeight;
    const int16_t bh = (i == count - 1) ? (h - y) : bandHeight;

    // Column 0: fillRect reference (correct color on this panel).
    tft.fillRect(0, y, colWidth, bh, color);

    if (buf != nullptr) {
      const size_t pixels = static_cast<size_t>(colWidth) * bh;
      for (size_t p = 0; p < pixels; ++p) {
        buf[p] = color;
      }
      // Column 1: pushImage with byte-swap ON.
      tft.setSwapBytes(true);
      tft.pushImage(colWidth, y, colWidth, bh, buf);
      // Column 2: pushImage with byte-swap OFF.
      tft.setSwapBytes(false);
      tft.pushImage(2 * colWidth, y, colWidth, bh, buf);
    }
  }

  if (buf != nullptr) {
    free(buf);
  }
  tft.setSwapBytes(CYD_TFT_SWAP_BYTES != 0);
  Serial.println(
      "color test card: col0=fillRect ref, col1=pushImage swapON, "
      "col2=pushImage swapOFF; rows R G B Y C M W");
}

#endif  // PANEL_CYD_TFT
