#pragma once

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
#if defined(TARGET_QUALIA_S3)

/** Full-screen status readout: Wi-Fi connection/SSID/signal, IP, hostname, and
 *  the configured home lat/lon. Re-draws current values on each call. */
void statusScreenInfo();
#endif
