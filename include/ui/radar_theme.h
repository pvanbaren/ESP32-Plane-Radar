#pragma once

#include <cstdint>

#include "config.h"

namespace ui::radar {

// All pixel dimensions below are authored for a 240 px display and scaled to
// the active panel via config::kUiScale (see config.h). scaledPx()/scaledPxF()
// round-trip to the original values on the 240 px build (kUiScale == 1).
constexpr int scaledPx(int px240) {
  return static_cast<int>(px240 * config::kUiScale + (px240 < 0 ? -0.5f : 0.5f));
}
constexpr float scaledPxF(float px240) { return px240 * config::kUiScale; }

// Text cap-height targets: panel scale plus the global font-size multiplier.
constexpr int fontPx(int px240) {
  return static_cast<int>(px240 * config::kUiScale * config::kUiFontScale + 0.5f);
}

constexpr int kSize = config::kDisplayWidth;
constexpr int kCenterX = kSize / 2;
constexpr int kCenterY = kSize / 2;

/** Outermost grid ring (inside edge labels). */
constexpr int kGridOuterRadius = scaledPx(107);

/** N: offset from top edge (top_center, negative = up). */
constexpr int kCardinalNorthOffsetY = scaledPx(-1);
/** S: offset from bottom edge (bottom_center, positive = down). */
constexpr int kCardinalSouthOffsetY = scaledPx(3);

/** Gap between scale label right edge and outer ring on the east spoke (px). */
constexpr int kScaleGapFromOuterRing = scaledPx(8);

/** Gap between the clock text and the outer ring (px), for top/bottom placement. */
constexpr int kClockGapFromOuterRing = scaledPx(4);

/** Clock VLW size (setTextSize factor). Panel-scaled so the clock shrinks with
 *  the display: 0.5x native on the 240 px build, 1.5x on the 720 px Qualia. */
constexpr float kClockVlwSize = scaledPxF(0.5f);

/** Target cap height (px) for N/S/E/W — full size (not reduced by the font scale). */
constexpr int kCardinalLabelHeightPx = scaledPx(14);
/** Target cap height (px) for the range/scale label (follows the font scale). */
constexpr int kScaleLabelHeightPx = fontPx(11);

constexpr int kRingCount = 4;

/** Shared grid stroke: drawWideLine half-width (2 px total); rings use the same
 *  px count. Deliberately NOT scaled with the panel — a fixed thin grid line
 *  reads better than a 6 px stroke at 720 px. */
constexpr float kGridStrokeHalfWidth = 1.0f;

constexpr int kCenterDotRadius = scaledPx(2);

/** Filled aircraft symbol (nose triangle). */
constexpr int kAircraftNoseLenPx = scaledPx(8);
constexpr int kAircraftTailLenPx = scaledPx(3);
constexpr int kAircraftTailHalfPx = scaledPx(4);
/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
/** Minimum visible vector when gs > 0 (px). */
constexpr int kAircraftSpeedLineMinPx = scaledPx(2);
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
/** Shorter than full 60 s horizon at ref scale; ×1.5 length boost applied. */
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;
/** drawWideLine half-width for speed vectors (2 px total). Fixed, not scaled —
 *  matches the thin grid stroke rather than growing to 6 px at 720. */
constexpr float kAircraftTrackLineHalfWidth = 1.0f;

constexpr float kRunwayLineWidthPx = scaledPxF(2.0f);
constexpr float kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
constexpr int kRunwayLabelHeightPx = fontPx(14);
constexpr int kRunwayLabelGapPx = scaledPx(3);
/** Gap from triangle edge to tag block (px). */
constexpr int kAircraftLabelGapPx = scaledPx(1);
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx =
    kAircraftNoseLenPx + kAircraftTailHalfPx + scaledPx(1);

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
constexpr int kBeyondRingDotRadiusPx = scaledPx(4);
constexpr int kBeyondRingScreenMarginPx = scaledPx(2);
/** Target cap height (px) for aircraft tags (slightly above scale label). */
constexpr int kAircraftTagLabelHeightPx = fontPx(13);

/** RGB565 palette targets (applied in initPalette). */
constexpr uint8_t kBgR = 4;
constexpr uint8_t kBgG = 10;
constexpr uint8_t kBgB = 28;
constexpr uint8_t kGridR = 16;
constexpr uint8_t kGridG = 100;
constexpr uint8_t kGridB = 32;
constexpr uint8_t kAircraftR = 255;
constexpr uint8_t kAircraftG = 0;
constexpr uint8_t kAircraftB = 0;
constexpr uint8_t kTrackR = 255;
constexpr uint8_t kTrackG = 0;
constexpr uint8_t kTrackB = 255;
constexpr uint8_t kTagTypeR = 255;
constexpr uint8_t kTagTypeG = 200;
constexpr uint8_t kTagTypeB = 0;
constexpr uint8_t kTagAltR = 90;
constexpr uint8_t kTagAltG = 200;
constexpr uint8_t kTagAltB = 255;
constexpr uint8_t kRunwayR = 56;
constexpr uint8_t kRunwayG = 150;
constexpr uint8_t kRunwayB = 170;
/** Lighter teal for ICAO labels (vs runway lines). */
constexpr uint8_t kRunwayLabelR = 110;
constexpr uint8_t kRunwayLabelG = 210;
constexpr uint8_t kRunwayLabelB = 230;
/** Water-body outlines: navy blue, 3 px lines. */
constexpr uint8_t kWaterR = 0;
constexpr uint8_t kWaterG = 0;
constexpr uint8_t kWaterB = 128;
/** drawWideLine half-width for water outlines (3 px total). Fixed, not scaled. */
constexpr float kWaterLineHalfWidth = 1.5f;

extern uint16_t kColorBackground;
extern uint16_t kColorGrid;
extern uint16_t kColorLabel;
extern uint16_t kColorCenter;
extern uint16_t kColorAircraft;
extern uint16_t kColorTrackVector;
extern uint16_t kColorTagType;
extern uint16_t kColorTagAltitude;
extern uint16_t kColorRunway;
extern uint16_t kColorRunwayLabel;
extern uint16_t kColorWater;

}  // namespace ui::radar
