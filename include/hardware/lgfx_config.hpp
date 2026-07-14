#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

#if defined(TARGET_QUALIA_S3)

/**
 * On the Qualia, `tft` is an off-screen LovyanGFX canvas (a full-screen sprite
 * in PSRAM). All drawing goes into it; finished frames are presented to the
 * panel by a separate esp_lcd RGB driver (hardware/qualia_rgb.*) configured for
 * double buffering + bounce, which makes the swap tear-free and flicker-free.
 *
 * The canvas is sized by displayInit() via createSprite(); the NV3052C register
 * init still runs over the TCA9554 expander (hardware/qualia_nv3052c.*).
 */
class LGFX : public lgfx::LGFX_Sprite {
 public:
  LGFX() {
    // esp_lcd reads the framebuffer as native little-endian RGB565. LovyanGFX's
    // default rgb565_2Byte is byte-swapped (for MSB-first SPI), so use the
    // non-swapped depth to feed esp_lcd directly without a per-frame swap.
    setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
    setPsram(true);
  }
};

#else

/** LovyanGFX device: GC9A01 on SPI. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

#endif  // TARGET_QUALIA_S3
