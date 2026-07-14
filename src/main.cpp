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

// ADS-B fetch runs on its own task: the HTTPS request blocks for ~1-2 s, and
// keeping it off the main loop lets the radar keep redrawing (dead-reckoned) at
// 2 Hz throughout. The task publishes into the shared aircraft buffer under a
// lock; the render loop reads a snapshot.
void adsbFetchTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      services::adsb::fetchUpdate(services::location::lat(),
                                  services::location::lon(),
                                  ui::radar::fetchRadiusKm());
    }
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
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
  services::adsb::init();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }

  // Start the background ADS-B fetch (pinned to core 0, away from the render
  // loop on core 1). It checks Wi-Fi state each cycle.
  xTaskCreatePinnedToCore(adsbFetchTask, "adsb", 16384, nullptr, 1, nullptr, 0);
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
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      // Redraw at 2 Hz with dead-reckoned positions; the ADS-B fetch runs on
      // its own task (adsbFetchTask).
      g_last_redraw_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }
  }

  delay(10);
}
