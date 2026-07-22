#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

#if defined(TARGET_QUALIA_S3)
// --- Display: NV3052C 4" round 720×720 (RGB666 parallel, Adafruit 5793) ---
// RGB data/sync pins live in hardware/lgfx_config.hpp (LovyanGFX Bus_RGB);
// the panel's register init + reset run over the TCA9554 I2C expander, see
// hardware/qualia_nv3052c.*. No per-pin GPIO constants are needed here.
constexpr int kDisplayWidth = 720;
constexpr int kDisplayHeight = 720;
// LovyanGFX Bus_RGB maps color565 to the panel in true RGB order, so the
// radar palette needs no R/B swap (unlike the BGR GC9A01 module).
constexpr bool kDisplayRgbOrder = false;
#else
// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;
#endif

// --- UI scaling ---
// The radar UI was laid out for a 240 px display. Every pixel dimension is
// expressed relative to that baseline and multiplied by kUiScale, so the same
// layout fills larger panels (e.g. the 720 px Qualia, kUiScale = 3)
// proportionally. On the 240 px build kUiScale == 1 and nothing changes.
constexpr int kUiBaseSize = 240;
constexpr float kUiScale = static_cast<float>(kDisplayWidth) / kUiBaseSize;

// The embedded VLW (data/ui_font.vlw) is rendered at 45 px native — 3× the
// 15 px font the original layout was tuned against. Fixed VLW size multipliers
// (e.g. status screens) divide by this so glyphs keep their intended on-screen
// size; the font is only ever downscaled (crisp). Adaptive text that searches
// by target pixel height needs no adjustment.
constexpr float kVlwNativeSizeScale = 45.0f / 15.0f;

// Global multiplier on rendered text size (independent of the panel scale).
// 1.0 = the baseline layout size; 0.5 = 50%.
constexpr float kUiFontScale = 0.5f;

// --- Radar center defaults (overridden via WiFi setup portal) ---
// KGRR — Gerald R. Ford International (Grand Rapids, MI). The small-airport
// dataset is filtered to within 100 mi of KGRR, so the default center matches.
constexpr double kDefaultRadarLat = 42.8808;
constexpr double kDefaultRadarLon = -85.5228;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
#if defined(TARGET_QUALIA_S3)

/** Status screen (DN tap): refresh cadence while shown, and how long before it
 *  auto-returns to the radar so the display doesn't sit on it indefinitely. */
constexpr unsigned long kStatusRefreshMs = 1000;
constexpr unsigned long kStatusScreenTimeoutMs = 20000;
#endif
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
