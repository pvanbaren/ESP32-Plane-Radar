#pragma once

#include <LovyanGFX.hpp>

bool displayFontInit();
bool displayFontIsSmooth();

/** Load the master VLW font on gfx (if smooth fonts are enabled). Use before
 *  drawing status/boot-screen text so it renders in the master font regardless
 *  of which per-size radar font was left loaded on this instance. */
bool displayFontEnsureLoaded(lgfx::LGFXBase& gfx);

/** VLW: setTextSize scale (1.0 = font point size). Bitmap: no-op — use displayFontSetBitmap. */
void displayFontSetSmoothSize(lgfx::LGFXBase& gfx, float size);

/** Select the embedded VLW font whose native height best matches target_px and
 *  load it on gfx (only when it differs from the one already loaded there). It
 *  is then rendered natively at size 1.0 unless the gap to target_px is worth a
 *  fractional rescale — more than 3% and more than one pixel — in which case it
 *  is scaled to land exactly on target_px. On the Qualia the per-size fonts
 *  usually stay native; on the C3 only the master exists, so most labels scale. */
void displayFontApplyHeight(lgfx::LGFXBase& gfx, float target_px);

/** Bitmap GFXfont fallback; clears any runtime VLW font on this instance. */
void displayFontSetBitmap(lgfx::LGFXBase& gfx, const lgfx::GFXfont* font);
