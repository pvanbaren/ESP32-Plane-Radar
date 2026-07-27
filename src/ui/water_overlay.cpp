#include "ui/water_overlay.h"

#include <cmath>

#include "data/water_bodies.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

// The water-body dataset is pre-filtered at build time to within 100 mi of the
// default center (KGRR), the same KGRR-specific scope as the small-airport data
// (see config.h). If the center is moved far via the portal, the outlines no
// longer match — regenerate with scripts/build_water_bodies.py for a new center.

namespace ui::water {
namespace {

constexpr float kKmPerDeg = 111.0f;

float e7ToDeg(int32_t e7) { return static_cast<float>(e7) * 1e-7f; }

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km) {
  *dx_km = static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  *dy_km = static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km =
      static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

// Water outlines extend past the grid rings, all the way to the screen edge (the
// round panel's rim = the largest circle centered on the display).
constexpr int kClipRadius = radar::kSize / 2;

// Does the segment come within the screen disc at all? (Both endpoints may be
// off-screen while the segment still crosses the visible area.)
bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = kClipRadius;
  const int r_sq = r * r;

  if (distSqFromCenter(x0, y0) <= r_sq || distSqFromCenter(x1, y1) <= r_sq) {
    return true;
  }

  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int fx = x0 - cx;
  const int fy = y0 - cy;
  const int a = dx * dx + dy * dy;
  if (a == 0) {
    return false;
  }
  const int b = 2 * (fx * dx + fy * dy);
  const int c = fx * fx + fy * fy - r_sq;
  int disc = b * b - 4 * a * c;
  if (disc < 0) {
    return false;
  }
  disc = static_cast<int>(sqrtf(static_cast<float>(disc)));
  const float inv2a = 1.0f / (2.0f * static_cast<float>(a));
  const float t0 = (-static_cast<float>(b) - disc) * inv2a;
  const float t1 = (-static_cast<float>(b) + disc) * inv2a;
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

// Pull (*x1,*y1) back along the segment toward (x0,y0) until it lands inside the
// screen disc, so a line never draws past the panel edge.
void clipPointToScreenEdge(int x0, int y0, int* x1, int* y1) {
  const int max_r = kClipRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

void drawSegment(lgfx::LGFXBase& gfx, int x0, int y0, int x1, int y1) {
  if (!segmentIntersectsDisc(x0, y0, x1, y1)) {
    return;
  }
  clipPointToScreenEdge(x0, y0, &x1, &y1);
  clipPointToScreenEdge(x1, y1, &x0, &y0);
  gfx.drawWideLine(x0, y0, x1, y1, radar::kWaterLineHalfWidth,
                   radar::kColorWater);
}

}  // namespace

void drawWaterBodies(lgfx::LGFXBase& gfx) {
  for (size_t p = 0; p < data::water_bodies::kPolylineCount; ++p) {
    const auto& pl = data::water_bodies::kPolylines[p];
    if (pl.count < 2) {
      continue;
    }

    int prev_x = 0;
    int prev_y = 0;
    int first_x = 0;
    int first_y = 0;
    for (uint16_t i = 0; i < pl.count; ++i) {
      const auto& pt = data::water_bodies::kPoints[pl.start + i];
      int x = 0;
      int y = 0;
      latLonToScreen(e7ToDeg(pt.lat_e7), e7ToDeg(pt.lon_e7), &x, &y);
      if (i == 0) {
        first_x = x;
        first_y = y;
      } else {
        drawSegment(gfx, prev_x, prev_y, x, y);
      }
      prev_x = x;
      prev_y = y;
    }
    // Closed rings (small lakes fully in range) draw the wrap-around segment.
    if (pl.closed) {
      drawSegment(gfx, prev_x, prev_y, first_x, first_y);
    }
  }
}

}  // namespace ui::water
