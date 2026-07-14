#pragma once

#if defined(TARGET_QUALIA_S3)

/**
 * esp_lcd RGB output stage for the Qualia panel.
 *
 * Uses two framebuffers (tear-free VSYNC swap) plus a bounce buffer (keeps the
 * scanout fed from internal SRAM so PSRAM traffic can't starve it -> no
 * flicker). Rendering draws DIRECTLY into the back framebuffer and present is a
 * pure pointer swap on VSYNC -- no per-frame copy.
 *
 * Call order: qualiaPanelInit() (NV3052C register init over the expander) THEN
 * qualiaRgbInit() (start RGB streaming and grab the framebuffers).
 */
bool qualiaRgbInit();

/** The framebuffer to draw the next frame into (720x720 RGB565). */
void* qualiaRgbBackBuffer();

/**
 * Present the just-drawn back buffer via a VSYNC framebuffer swap (no copy),
 * then flip so the next draw targets the other (now-free) buffer.
 */
void qualiaRgbPresent();

#endif  // TARGET_QUALIA_S3
