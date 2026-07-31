#include "hardware/display_font.h"

#include <cmath>

#include "hardware/display.h"

// The master font (ui_font.vlw) is always embedded. On the Qualia build a set
// of per-size fonts is also embedded, each rendered natively at one exact
// on-screen height the 720 px radar uses (see scripts/build_ui_font.py and
// platformio.ini). LovyanGFX's VLW scaler is nearest-neighbour, so drawing a
// label from a font rendered at its target height is crisper than scaling the
// master; displayFontApplyHeight() picks the closest and renders it natively
// unless the size gap is worth a fractional rescale (>3% and >1 px). On the C3
// build only the master exists, so most labels exceed that threshold and get
// downscaled from the 45 px master.
extern "C" {
extern const uint8_t _binary_data_ui_font_vlw_start[] asm(
    "_binary_data_ui_font_vlw_start");
extern const uint8_t _binary_data_ui_font_vlw_end[] asm("_binary_data_ui_font_vlw_end");
#if defined(TARGET_QUALIA_S3)
#define VLW_SYM(name) \
  extern const uint8_t name[] asm(#name)
VLW_SYM(_binary_data_ui_font_14_vlw_start);
VLW_SYM(_binary_data_ui_font_16_vlw_start);
VLW_SYM(_binary_data_ui_font_17_vlw_start);
VLW_SYM(_binary_data_ui_font_18_vlw_start);
VLW_SYM(_binary_data_ui_font_21_vlw_start);
VLW_SYM(_binary_data_ui_font_42_vlw_start);
VLW_SYM(_binary_data_ui_font_69_vlw_start);
#undef VLW_SYM
#endif
}

namespace {

struct FontEntry {
  const uint8_t* data;
  float native_h;  // fontHeight() at size 1.0, measured in displayFontInit()
};

// The master is entry 0; displayFontEnsureLoaded() always selects it.
FontEntry s_fonts[] = {
    {_binary_data_ui_font_vlw_start, 0.0f},
#if defined(TARGET_QUALIA_S3)
    {_binary_data_ui_font_14_vlw_start, 0.0f},
    {_binary_data_ui_font_16_vlw_start, 0.0f},
    {_binary_data_ui_font_17_vlw_start, 0.0f},
    {_binary_data_ui_font_18_vlw_start, 0.0f},
    {_binary_data_ui_font_21_vlw_start, 0.0f},
    {_binary_data_ui_font_42_vlw_start, 0.0f},
    {_binary_data_ui_font_69_vlw_start, 0.0f},
#endif
};
constexpr size_t kFontCount = sizeof(s_fonts) / sizeof(s_fonts[0]);

bool s_vlw_loaded = false;

// One-slot cache of the font currently loaded on a given instance, so contiguous
// draws in the same size (e.g. all four N/S/E/W labels) reload only once.
lgfx::LGFXBase* s_active_gfx = nullptr;
const uint8_t* s_active_data = nullptr;

size_t vlwDataLen() {
  return static_cast<size_t>(_binary_data_ui_font_vlw_end -
                               _binary_data_ui_font_vlw_start);
}

// Load `data` on gfx, skipping the reload when it is already the active font.
bool useFont(lgfx::LGFXBase& gfx, const uint8_t* data) {
  if (s_active_gfx == &gfx && s_active_data == data) {
    return true;
  }
  if (!gfx.loadFont(data, lgfx::IFont::font_type_t::ft_vlw)) {
    return false;
  }
  s_active_gfx = &gfx;
  s_active_data = data;
  return true;
}

}  // namespace

bool displayFontInit() {
  s_vlw_loaded = vlwDataLen() > 0 && useFont(tft, s_fonts[0].data);
  if (!s_vlw_loaded) {
    Serial.println("Smooth font load failed — using bitmap fallback");
    return false;
  }
  // Measure each embedded font's native height once (needed to pick the best
  // match and compute the residual scale in displayFontApplyHeight()).
  for (size_t i = 0; i < kFontCount; ++i) {
    if (useFont(tft, s_fonts[i].data)) {
      tft.setTextSize(1.0f);
      s_fonts[i].native_h = static_cast<float>(tft.fontHeight());
    }
    if (s_fonts[i].native_h <= 0.0f) {
      s_fonts[i].native_h = 1.0f;  // guard against divide-by-zero
    }
  }
  useFont(tft, s_fonts[0].data);  // leave the master active
  return true;
}

bool displayFontIsSmooth() { return s_vlw_loaded; }

bool displayFontEnsureLoaded(lgfx::LGFXBase& gfx) {
  if (!s_vlw_loaded) {
    return false;
  }
  return useFont(gfx, s_fonts[0].data);
}

void displayFontSetSmoothSize(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);
}

void displayFontApplyHeight(lgfx::LGFXBase& gfx, float target_px) {
  if (!s_vlw_loaded) {
    return;
  }
  size_t best = 0;
  float best_err = -1.0f;
  for (size_t i = 0; i < kFontCount; ++i) {
    const float err = std::fabs(s_fonts[i].native_h - target_px);
    if (best_err < 0.0f || err < best_err) {
      best_err = err;
      best = i;
    }
  }
  useFont(gfx, s_fonts[best].data);
  const float native_h = s_fonts[best].native_h;
  const float scale = target_px / native_h;
  // Only fractionally rescale when the closest native size is off by enough to
  // matter: both more than 3% and more than one pixel. LovyanGFX's VLW scaler is
  // nearest-neighbour, so a tiny rescale only softens the glyphs without a
  // visible size change — render native (1.0) in that case. On the Qualia the
  // per-size fonts almost always land within the threshold and stay native; on
  // the C3, with only the master, larger deltas fall through to a downscale.
  const bool worth_rescaling =
      std::fabs(scale - 1.0f) > 0.03f && std::fabs(target_px - native_h) > 1.0f;
  gfx.setTextSize(worth_rescaling ? scale : 1.0f);
}

void displayFontSetBitmap(lgfx::LGFXBase& gfx, const lgfx::GFXfont* font) {
  gfx.setFont(font);
  gfx.setTextSize(1);
  // A bitmap GFXfont is now active; drop the VLW cache so the next VLW request
  // reloads rather than assuming its font is still resident.
  if (s_active_gfx == &gfx) {
    s_active_gfx = nullptr;
    s_active_data = nullptr;
  }
}
