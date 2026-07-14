#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

// Push the current frame to the panel. On the Qualia this hands the off-screen
// canvas to the double-buffered esp_lcd RGB driver (tear/flicker-free); on the
// C3 the panel is written live, so this is a no-op.
void displayPresent();

// Phase-2 bring-up helper: draws a color/ring test pattern to validate the
// panel (color order, timing, round geometry) independently of the radar UI.
void displayTestPattern();
