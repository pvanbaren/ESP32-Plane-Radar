#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

#if defined(TARGET_QUALIA_S3)

// The ESP32-S3 RGB-parallel classes are not pulled in by <LovyanGFX.hpp>;
// include them explicitly (as the library's own RGB user-configs do).
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

/**
 * LovyanGFX device for the Adafruit Qualia ESP32-S3 driving the 4" round
 * 720x720 NV3052C panel (Adafruit 5793) over the RGB666 parallel bus.
 *
 * Bus_RGB only streams pixels — it has no command channel. The NV3052C
 * register init and hardware reset are performed separately over the on-board
 * TCA9554 I2C expander (see hardware/qualia_nv3052c.*), which must run BEFORE
 * tft.init().
 *
 * Pin map and timings are taken from Adafruit's CircuitPython board/display
 * definitions for this exact board+panel:
 *   - data_gpio_nums are LSB->MSB: blue[0..4], green[0..5], red[0..4]
 *   - LovyanGFX pin_dN is data bit N, so the mapping is 1:1.
 */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;

 public:
  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = config::kDisplayWidth;
      cfg.memory_height = config::kDisplayHeight;
      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;  // 720x720x16bpp (~1 MB) framebuffer in PSRAM
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;

      // Blue, LSB -> MSB  (data bits 0..4)
      cfg.pin_d0 = 40;
      cfg.pin_d1 = 39;
      cfg.pin_d2 = 38;
      cfg.pin_d3 = 0;
      cfg.pin_d4 = 45;
      // Green, LSB -> MSB (data bits 5..10)
      cfg.pin_d5 = 48;
      cfg.pin_d6 = 47;
      cfg.pin_d7 = 21;
      cfg.pin_d8 = 14;
      cfg.pin_d9 = 13;
      cfg.pin_d10 = 12;
      // Red, LSB -> MSB   (data bits 11..15)
      cfg.pin_d11 = 11;
      cfg.pin_d12 = 10;
      cfg.pin_d13 = 9;
      cfg.pin_d14 = 46;
      cfg.pin_d15 = 3;

      cfg.pin_henable = 2;  // DE
      cfg.pin_vsync = 42;
      cfg.pin_hsync = 41;
      cfg.pin_pclk = 1;     // DCLK

      cfg.freq_write = 16000000;  // 16 MHz dot clock

      // Timings from the NV3052C round-panel definition. If the image is
      // torn/shifted or colors are wrong, these polarity flags are the first
      // things to flip (see docs/qualia_display.md).
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 46;
      cfg.hsync_pulse_width = 2;
      cfg.hsync_back_porch = 44;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 50;
      cfg.vsync_pulse_width = 16;
      cfg.vsync_back_porch = 16;
      cfg.pclk_active_neg = 0;  // pclk_active_high = true
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    setPanel(&_panel);
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
