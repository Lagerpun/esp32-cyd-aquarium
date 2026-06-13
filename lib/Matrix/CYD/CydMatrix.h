#pragma once

#ifdef PANEL_CYD_TFT

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "CydMatrixSettings.h"
#include "GFX_Layer.hpp"
#include "Matrix.h"

#ifndef CYD_FRAMEBUFFER_RENDERER
#define CYD_FRAMEBUFFER_RENDERER 0
#endif

#ifndef CYD_DOT_RENDERER
#define CYD_DOT_RENDERER 0
#endif

class CydMatrix : public Matrix {
 private:
  TFT_eSPI tft;
  uint16_t* frameBuffer;
  uint16_t* rowBuffer;
  uint16_t* previousLogicalPixels;
  int16_t rowBufferLogicalY;
  uint16_t rowBufferY0;
  uint16_t rowBufferY1;
  bool rowBufferDirty;
  uint16_t dirtyRowStart;
  uint16_t dirtyRowEnd;
  bool backlightPwmAttached;
  uint8_t backlightPercent;

  uint16_t packRgb565ForDisplay(uint8_t r, uint8_t g, uint8_t b,
                                uint8_t brightnessScale,
                                uint8_t colorProfile);
  uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b);
  void allocateFrameBuffer();
  void allocateRowBuffer();
  void allocatePreviousLogicalPixels();
  void clearFrameBuffer(uint16_t color);
  void markFrameBufferRowsDirty(uint16_t rowStart, uint16_t rowEnd);
  void pushFrameBuffer();
  void resetRowBuffer(uint16_t logicalY);
  void flushRowBuffer();
  void writeRowBufferPixel(uint16_t x, uint16_t y, uint16_t color);
  void writeFrameBufferDot(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1,
                           uint16_t color);
  void writeScaledLogicalPixel(uint16_t x, uint16_t y, uint16_t color);
  void initBacklightPwm();
  void writeBacklightPercent(uint8_t percent);
  void setBacklightEnabled(bool enabled);

 public:
  CydMatrix();

  void init() override;

  void drawPixelRGB888(uint16_t x, uint16_t y, uint8_t r_data, uint8_t g_data,
                       uint8_t b_data) override;
  void drawPixelRGB888Profile(uint16_t x, uint16_t y, uint8_t r_data,
                              uint8_t g_data, uint8_t b_data,
                              bool foregroundProfile);

  void setBrightness(uint8_t newBrightness) override;
  uint8_t getBrightness() const override;
  uint8_t getXResolution() override;
  uint8_t getYResolution() override;

  void setRotation(uint8_t newRotation) override;
  void rotate90() override;
  void clearScreen() override;
  void compositeLayers() override;

  bool isFrameBufferActive() const;
  bool isRowBufferActive() const;
  void setBacklightPercent(uint8_t percent);
  uint8_t getBacklightPercent() const;
  void setColorDiagnostic(bool invert, bool swap);
  void runStartupSmokeTest();
  void update() override;
};

#endif  // PANEL_CYD_TFT
