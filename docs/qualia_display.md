# Qualia ESP32-S3 + 4" round 720×720 (NV3052C) bring-up

This documents the Phase 1–2 port of Plane Radar to the **Adafruit Qualia
ESP32-S3 for RGB666** (product 5800) driving the **4" round 720×720 NV3052C**
panel (product 5793, "P5793A").

The original ESP32-C3 + GC9A01 build (`env:supermini`) is unchanged; the Qualia
target is a separate `env:qualia_s3`.

## How the display is driven

The NV3052C is an RGB-parallel ("DotClock") panel: the ESP32-S3 LCD peripheral
streams pixels continuously over 16 data lines + PCLK/HSYNC/VSYNC/DE. That path
has **no command channel**, so the panel's power-on register init and reset are
done separately over the on-board **TCA9554 I²C expander** (addr `0x3F`) as a
bit-banged 9-bit SPI transaction.

Three pieces, in `displayInit()` order:

1. `hardware/qualia_nv3052c.*` — `qualiaPanelInit()`: reset + NV3052C register
   init over the expander. Transcribed from Adafruit's CircuitPython
   `displays/round40.py`, so it matches a known-good config.
2. `hardware/qualia_rgb.*` — `qualiaRgbInit()`: the output stage, an **esp_lcd
   RGB panel with two framebuffers + a bounce buffer**. Two framebuffers make
   the swap happen on VSYNC (tear-free); the bounce buffer feeds the scanout
   from internal SRAM so a big PSRAM blit can't starve it (flicker-free). Needs
   IDF 5.x (see Toolchain above).
3. `hardware/lgfx_config.hpp` — `tft` is a LovyanGFX **off-screen canvas**
   (720×720 sprite in PSRAM). All drawing (radar, status, test pattern) targets
   it; `displayPresent()` hands the finished frame to `qualiaRgbPresent()` which
   calls `esp_lcd_panel_draw_bitmap` for the tear/glitch-free swap. On the C3,
   `displayPresent()` is a no-op (its SPI panel is written live).

## Toolchain (IDF 5.x)

The `qualia_s3` env uses the **pioarduino** platform (arduino-esp32 3.3.9 /
ESP-IDF 5.5.4) instead of stock `espressif32@6.x` — IDF 5.x is required for the
esp_lcd RGB **double framebuffer + bounce buffer** that makes redraws
tear/flicker-free (IDF 4.4 has none of those APIs). The C3 `supermini` env is
unchanged (stock `espressif32@6.5.0`).

Running the pioarduino *platform* under stock PlatformIO core needs two one-time
fixes to the PlatformIO virtualenv (`~/.platformio/penv`) — done once per
machine:

```bash
# 1. Python deps the pioarduino platform scripts expect
~/.platformio/penv/Scripts/python -m pip install requests urllib3 pyelftools esptool

# 2. esptool: the platform tries an editable install of a tool dir that has no
#    Python module under stock PIO. builder/penv_setup.py::install_esptool is
#    patched locally to `uv pip install esptool` (normal) instead of `-e <dir>`.
```

If a `pio` build ever reports "Failed to install Python dependencies into penv"
or "No module named esptool/requests/urllib3", re-run step 1.

## Build & flash

```bash
# Test-pattern build (default: -DDISPLAY_TEST_PATTERN is set in platformio.ini)
pio run -e qualia_s3 -t upload
pio device monitor -e qualia_s3        # 115200 baud

# Single-file web-flash image (esptool-js etc.), ESP32-S3, 16 MB, flash at 0x0
pio run -e qualia_s3 -t merge
# -> .pio/build/qualia_s3/firmware-merged.bin
```

Put the board in download mode if auto-reset fails (hold **BOOT**, tap
**RESET**), then flash over USB.

## What the test pattern shows

With `-DDISPLAY_TEST_PATTERN` the firmware skips WiFi/radar and draws:

- Six labelled color bars: **RED, GREEN, BLUE, YELLOW, CYAN, WHITE**
- Green concentric rings + crosshair centered on the panel
- A white bounding circle at the panel edge
- `720x720` text near the center

Serial should print `NV3052C panel init sent`. If it prints `TCA9554 not found
at 0x3F`, the expander isn't responding — check the board and that no other code
grabbed the I²C pins (SDA 8 / SCL 18).

## Troubleshooting (the finicky RGB knobs)

Change one thing at a time; all live in `hardware/lgfx_config.hpp`.

| Symptom | Likely fix |
|---|---|
| **All colors wrong / look byte-swapped** | The canvas byte order doesn't match esp_lcd. Add `tft.setSwapBytes(true)` in `displayInit()` after `createSprite`, or feed a swapped buffer in `qualiaRgbPresent`. First thing to try if colors are off globally. |
| **Red and blue swapped** (bar labeled RED is blue, etc.) | Swap the blue block (`kDataPins[0..4]`) with the red block (`[11..15]`) in `qualia_rgb.cpp`. Also set `config::kDisplayRgbOrder = true` so the radar palette matches. |
| **Colors garbled / bit-reversed within a channel** | Reverse the pin order inside that channel's block in `kDataPins` (LSB↔MSB). |
| **Image torn, shimmering, or horizontally shifted** | Flip `cfg.timings.flags.pclk_active_neg` (0↔1) in `qualia_rgb.cpp`. Most common single fix. |
| **Image rolls / offset vertically or horizontally** | Adjust the porch values in `cfg.timings` (`hsync_*`, `vsync_*`). |
| **Periodic flicker still present** | Increase `cfg.bounce_buffer_size_px` (e.g. `kW * 20`). |
| **`esp_lcd_new_rgb_panel failed` on serial** | If `num_fbs = 2` + bounce is rejected, try `num_fbs = 2` with `bounce_buffer_size_px = 0`, or bounce with `num_fbs = 1`. |
| **Blank / black screen, init OK on serial** | Lower `cfg.timings.pclk_hz` (e.g. 12 MHz); confirm PSRAM (`qio_opi`) and that the canvas allocated. |
| **Dim or off backlight** | On this board the expander backlight bit is left as an input (panel default-on). If your panel needs it driven, drive expander bit 4 high after init. |

### Reference timings (NV3052C round 720×720)

| Param | Value |
|---|---|
| Dot clock | 16 MHz |
| HSYNC pulse / back / front | 2 / 44 / 46 |
| VSYNC pulse / back / front | 16 / 16 / 50 |
| PCLK | active high, idle low |
| HSYNC / VSYNC | idle high |
| DE | idle low |

### TCA9554 expander (addr 0x3F) bit map

| Bit | Signal | Direction |
|---|---|---|
| 0 | TFT SCK | out |
| 1 | TFT CS | out |
| 2 | TFT RESET | out |
| 3 | Touch IRQ | in |
| 4 | Backlight | in (default-on) |
| 5 | Button UP | in |
| 6 | Button DOWN | in |
| 7 | TFT MOSI | out |

## Radar UI scaling (Phase 3)

The full radar app is now the default build (`-DDISPLAY_TEST_PATTERN` is
commented out). The UI scales from its original 240 px layout to the panel size
via `config::kUiScale` (= `kDisplayWidth / 240`, so 3× at 720 px):

- **Geometry** — all pixel dimensions in `ui/radar_theme.h` go through
  `scaledPx()`/`scaledPxF()`; rings, symbols, stroke widths, and label spacing
  scale proportionally. The 240 px (C3) build is unchanged (`kUiScale == 1`).
- **Status screens** — `ui/status_screens.cpp` scales its spinner/layout px and
  VLW font sizes the same way.
- **Fonts** — the embedded VLW (`data/ui_font.vlw`) is Noto Sans Regular
  rendered at **45 px native** (3× the original 15 px), so text is only ever
  *downscaled* and stays crisp on both builds. On-screen size is `config::kUiScale`
  (panel) × `config::kUiFontScale` (global text multiplier, currently 0.8). Fixed
  VLW multipliers (status screens) also divide by `config::kVlwNativeSizeScale`;
  adaptive radar labels (searched by target pixel height) pick up the factors via
  `fontPx()`. Regenerate (any weight/TTF) with:

  ```bash
  python scripts/build_ui_font.py NotoSans-Regular.ttf   # -> data/ui_font.vlw
  ```

## Controls (Phase 4)

The C3's single BOOT button (GPIO 9) is an RGB data line on the Qualia, so
controls use the two **TCA9554 buttons** instead (polled over I²C, active-low):

| Button | Gesture | Effect |
|--------|---------|--------|
| **UP** (expander bit 5) | tap | Cycle range preset (5 → 10 → 15 → 25 km) |
| **DN** (expander bit 6) | hold 3 s | Clear Wi-Fi / location / units, reboot to setup |

If your board doesn't populate these buttons, the controls simply never
trigger (range still defaults and persists via NVS). If a button reads
inverted, flip the active-low sense in `qualiaButtonMask()`
(`hardware/qualia_nv3052c.cpp`).
