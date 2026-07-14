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
bit-banged 9-bit SPI transaction:

- `hardware/qualia_nv3052c.*` — expander + init sequence (runs first)
- `hardware/lgfx_config.hpp` — LovyanGFX `Bus_RGB` + `Panel_RGB` (pixel streaming)

`displayInit()` calls `qualiaPanelInit()` before `tft.init()`.

The init sequence and all pin/timing values are transcribed from Adafruit's
CircuitPython board + `displays/round40.py` definitions for this exact
board+panel, so they match a known-good configuration.

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
| **Red and blue swapped** (bar labeled RED is blue, etc.) | Swap the `pin_d0..d4` (blue) block with `pin_d11..d15` (red). Also set `config::kDisplayRgbOrder = true` so the radar palette matches. |
| **Colors garbled / bit-reversed within a channel** | Reverse the pin order inside that channel's block (LSB↔MSB). |
| **Image torn, shimmering, or horizontally shifted** | Flip `cfg.pclk_active_neg` (0↔1). This is the most common single fix. |
| **Image rolls / offset vertically or horizontally** | Adjust the porch values (`hsync_*`, `vsync_*`); start from the table below. |
| **Blank / black screen, init OK on serial** | Try lowering `cfg.freq_write` (e.g. 12 MHz); confirm PSRAM is enabled (`qio_opi`). |
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
- **Fonts** — the embedded VLW (`data/ui_font.vlw`) is rendered at **45 px
  native** (3× the original 15 px), so text is only ever *downscaled* and stays
  crisp on both builds. Fixed VLW multipliers (status screens) divide by
  `config::kVlwNativeSizeScale`; adaptive radar labels (searched by target pixel
  height) need no adjustment. Regenerate with:

  ```bash
  python scripts/build_ui_font.py NotoSans-Bold.ttf   # -> data/ui_font.vlw
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
