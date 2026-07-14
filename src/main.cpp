/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/qualia_nv3052c.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();

#if defined(TARGET_QUALIA_S3)
  // Poll the UP button (TCA9554) for a tap → cycle range. The C3 does this via
  // a GPIO interrupt, but the expander has no interrupt line, so edge-detect
  // the release here with a short debounce (kBootTapMinMs).
  static bool up_was_down = false;
  static unsigned long up_down_ms = 0;
  const bool up_down = (qualiaButtonMask() & kQualiaBtnUp) != 0;
  if (up_down && !up_was_down) {
    up_down_ms = millis();
  } else if (!up_down && up_was_down &&
             millis() - up_down_ms >= config::kBootTapMinMs) {
    onRangeTap();
  }
  up_was_down = up_down;
#endif

  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

#if defined(DISPLAY_TEST_PATTERN)
  // Phase-2 bring-up: verify the panel on its own, then stop. No BOOT button
  // (its GPIO collides with an RGB data line on the Qualia) and no WiFi/radar.
  Serial.println("Display test pattern mode");
  displayInit();
  displayTestPattern();
  return;
#endif

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
#if defined(DISPLAY_TEST_PATTERN)
  delay(1000);
  return;
#endif

  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
