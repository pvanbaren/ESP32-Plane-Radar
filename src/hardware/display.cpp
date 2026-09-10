#include "hardware/display.h"

#include "hardware/display_font.h"
#include "hardware/qualia_nv3052c.h"

LGFX tft;

void displayInit() {
#if defined(TARGET_QUALIA_S3)
  // The RGB bus can't send panel commands, so reset + register-init the
  // NV3052C over the I2C expander before LovyanGFX starts streaming pixels.
  if (!qualiaPanelInit()) {
    Serial.println("Panel init failed — display may stay blank");
  }
#endif
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}

void displayTestPattern() {
  const int w = tft.width();
  const int h = tft.height();
  const int cx = w / 2;
  const int cy = h / 2;
  const int radius = (w < h ? w : h) / 2 - 1;

  tft.startWrite();
  tft.fillScreen(tft.color888(0, 0, 0));

  // Primary-color bars across the top half — a wrong R/B ordering is obvious.
  struct Bar {
    uint8_t r, g, b;
    const char* label;
  };
  const Bar bars[] = {
      {255, 0, 0, "RED"},     {0, 255, 0, "GREEN"}, {0, 0, 255, "BLUE"},
      {255, 255, 0, "YELLOW"}, {0, 255, 255, "CYAN"}, {255, 255, 255, "WHITE"},
  };
  const int n = sizeof(bars) / sizeof(bars[0]);
  const int bar_h = h / (2 * n);
  tft.setTextDatum(lgfx::textdatum_t::middle_left);
  tft.setTextSize(2.0f);
  for (int i = 0; i < n; ++i) {
    const int y = i * bar_h;
    tft.fillRect(0, y, w, bar_h, tft.color888(bars[i].r, bars[i].g, bars[i].b));
    // Contrast label (black on light bars, white on dark).
    const bool light = (bars[i].r + bars[i].g + bars[i].b) > 300;
    tft.setTextColor(light ? tft.color888(0, 0, 0) : tft.color888(255, 255, 255));
    tft.drawString(bars[i].label, 24, y + bar_h / 2);
  }

  // Concentric rings + crosshair centered on the panel — checks geometry and
  // that the round bezel is centered.
  const uint16_t green = tft.color888(0, 200, 0);
  for (int r = radius; r > 0; r -= radius / 6) {
    tft.drawCircle(cx, cy, r, green);
  }
  tft.drawFastHLine(0, cy, w, green);
  tft.drawFastVLine(cx, 0, h, green);
  tft.drawCircle(cx, cy, radius, tft.color888(255, 255, 255));
  tft.fillCircle(cx, cy, 5, tft.color888(255, 255, 255));

  // Resolution readout at the center.
  tft.setTextDatum(lgfx::textdatum_t::middle_center);
  tft.setTextColor(tft.color888(255, 255, 255));
  tft.setTextSize(2.5f);
  char label[24];
  snprintf(label, sizeof(label), "%dx%d", w, h);
  tft.drawString(label, cx, cy + radius / 2);

  tft.endWrite();
}
