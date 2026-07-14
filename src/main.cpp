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
unsigned long g_last_redraw_ms = 0;
#if defined(TARGET_QUALIA_S3)
bool g_status_visible = false;
unsigned long g_status_shown_ms = 0;
unsigned long g_last_status_refresh_ms = 0;
#endif

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

#if defined(TARGET_QUALIA_S3)
  // Don't repaint the radar over the status screen while it's up; the new range
  // takes effect and shows when the status screen is dismissed.
  if (g_status_visible) {
    return;
  }
#endif
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}
#if defined(TARGET_QUALIA_S3)

// DN tap toggles the status readout (IP, Wi-Fi, home lat/lon). Rendering and the
// auto-return timeout are handled in loop(); this just flips the state and forces
// an immediate (re)draw of whichever view should now be showing.
void toggleStatusScreen() {
  g_status_visible = !g_status_visible;
  g_status_shown_ms = millis();
  g_last_status_refresh_ms = 0;  // draw the status screen on the next loop pass
  if (!g_status_visible) {
    g_last_redraw_ms = 0;  // return to the radar immediately
  }
}
#endif

void handleBootButton() {
  bootButtonPollLongPress();

#if defined(TARGET_QUALIA_S3)
  // Taps are latched by the background task (qualiaButtonsStart), so they're
  // caught even while this loop is blocked in an ADS-B fetch. UP → range up,
  // DN → status screen.
  if (qualiaConsumeUpTap()) {
    onRangeTap();
  }
  if (qualiaConsumeDownTap()) {
    toggleStatusScreen();
  }
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
  displayPresent();
  return;
#endif

  bootButtonInit();
  displayInit();
#if defined(TARGET_QUALIA_S3)
  qualiaButtonsStart();  // async button polling (I2C is up after displayInit)
#endif
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

#if defined(TARGET_QUALIA_S3)
  if (g_status_visible) {
    if (millis() - g_status_shown_ms >= config::kStatusScreenTimeoutMs) {
      // Auto-return to the radar; fall through and let it redraw this pass.
      g_status_visible = false;
      g_last_redraw_ms = 0;
    } else {
      if (millis() - g_last_status_refresh_ms >= config::kStatusRefreshMs) {
        g_last_status_refresh_ms = millis();
        statusScreenInfo();  // refresh IP/RSSI/etc. while shown
      }
      delay(10);
      return;
    }
  }

#endif
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
      // Refresh aircraft data from adsb.fi (~every 3 s); also redraws.
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
      g_last_redraw_ms = millis();
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      // Between fetches, redraw at 2 Hz with dead-reckoned positions.
      g_last_redraw_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }
  }

  delay(10);
}
