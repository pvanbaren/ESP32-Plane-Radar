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

/** Select the embedded VLW font whose native height best matches target_px,
 *  load it on gfx (only when it differs from the one already loaded there), and
 *  set the residual text size to land exactly on target_px. On the Qualia build
 *  the per-size fonts make the residual ~1.0 (native, crisp); on the C3 build
 *  only the master exists, so this reduces to a downscale of the 45 px master. */
void displayFontApplyHeight(lgfx::LGFXBase& gfx, float target_px);

/** Bitmap GFXfont fallback; clears any runtime VLW font on this instance. */
void displayFontSetBitmap(lgfx::LGFXBase& gfx, const lgfx::GFXfont* font);
