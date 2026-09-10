#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

// Phase-2 bring-up helper: draws a color/ring test pattern to validate the
// panel (color order, timing, round geometry) independently of the radar UI.
void displayTestPattern();
