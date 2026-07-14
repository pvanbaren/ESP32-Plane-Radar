#include "hardware/qualia_rgb.h"

#if defined(TARGET_QUALIA_S3)

#include <Arduino.h>

#include <cstring>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_private/periph_ctrl.h"
#include "soc/periph_defs.h"

namespace {

constexpr int kW = 720;
constexpr int kH = 720;

esp_lcd_panel_handle_t s_panel = nullptr;
void* s_fb[2] = {nullptr, nullptr};
int s_back = 0;  // framebuffer index to draw the next frame into

// RGB data pins, LSB->MSB: blue[0..4], green[0..5], red[0..4]. Matches the
// board's data_gpio_nums (see docs/qualia_display.md).
const int kDataPins[16] = {40, 39, 38, 0,  45, 48, 47, 21,
                           14, 13, 12, 11, 10, 9,  46, 3};

}  // namespace

bool qualiaRgbInit() {
  // A soft reboot (esp_restart, watchdog, crash) resets the CPU but leaves the
  // panel powered and LCD_CAM/GDMA possibly still clocking. esp_lcd_new_rgb_panel
  // then latches the scan-out DMA onto whatever VSYNC phase is current, so
  // framebuffer line 0 can land partway down the panel — the whole image is
  // shifted vertically (N below S) and stays that way until a full power cycle.
  // Resetting the peripheral first makes a warm boot start from the same clean
  // state as a cold boot, where DMA and sync generator start aligned.
  periph_module_reset(PERIPH_LCD_CAM_MODULE);

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;

  cfg.timings.pclk_hz = 16 * 1000 * 1000;  // 16 MHz dot clock
  cfg.timings.h_res = kW;
  cfg.timings.v_res = kH;
  cfg.timings.hsync_pulse_width = 2;
  cfg.timings.hsync_back_porch = 44;
  cfg.timings.hsync_front_porch = 46;
  cfg.timings.vsync_pulse_width = 16;
  cfg.timings.vsync_back_porch = 16;
  cfg.timings.vsync_front_porch = 50;
  cfg.timings.flags.hsync_idle_low = 0;
  cfg.timings.flags.vsync_idle_low = 0;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 0;  // pclk active high
  cfg.timings.flags.pclk_idle_high = 0;

  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;                      // double buffer -> tear-free swap
  // Bounce buffer headroom against PSRAM stalls during the present copy. Too
  // little -> the LCD FIFO underruns and the frame slips a few lines until the
  // next VSYNC (intermittent ~N-line shift). The line count MUST divide the
  // frame height (720) or esp_lcd rejects it with ESP_ERR_INVALID_ARG (0x102);
  // 36 lines (720/36 = 20) is ~2 ms at 16 MHz.
  cfg.bounce_buffer_size_px = kW * 36;
  cfg.dma_burst_size = 64;

  cfg.hsync_gpio_num = 41;
  cfg.vsync_gpio_num = 42;
  cfg.de_gpio_num = 2;
  cfg.pclk_gpio_num = 1;
  cfg.disp_gpio_num = -1;
  for (int i = 0; i < 16; ++i) {
    cfg.data_gpio_nums[i] = kDataPins[i];
  }
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err != ESP_OK) {
    Serial.printf("esp_lcd_new_rgb_panel failed: 0x%x\n", err);
    s_panel = nullptr;
    return false;
  }
  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);

  // Grab the two framebuffers for zero-copy ping-pong presenting.
  if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &s_fb[0], &s_fb[1]) !=
      ESP_OK) {
    Serial.println("get_frame_buffer failed");
    s_fb[0] = s_fb[1] = nullptr;
    return false;
  }
  // Clear both so nothing garbage shows before the first frames are drawn.
  memset(s_fb[0], 0, static_cast<size_t>(kW) * kH * 2);
  memset(s_fb[1], 0, static_cast<size_t>(kW) * kH * 2);
  s_back = 0;
  // Belt-and-suspenders on top of the peripheral reset: ask the driver to
  // re-sync the scan-out DMA to VSYNC (deferred to its next VSYNC handler). This
  // is the espressif-documented remedy for a "permanent shift" and also clears
  // any residual vertical offset should the reset above not fully re-align.
  esp_lcd_rgb_panel_restart(s_panel);
  Serial.println("esp_lcd RGB panel started (2 fb ping-pong + bounce)");
  return true;
}

void* qualiaRgbBackBuffer() { return s_fb[s_back]; }

void qualiaRgbPresent() {
  if (s_panel == nullptr || s_fb[s_back] == nullptr) {
    return;
  }
  // Present the framebuffer we just drew into: draw_bitmap with one of the
  // driver's own framebuffers switches to it on VSYNC (no copy) and blocks
  // until the previous buffer is free. Then flip so the next draw targets it.
  esp_lcd_panel_draw_bitmap(s_panel, 0, 0, kW, kH, s_fb[s_back]);
  s_back ^= 1;
}

#endif  // TARGET_QUALIA_S3
