#!/usr/bin/env python3
"""Build a water-body outline dataset from Natural Earth 10m lakes.

Natural Earth's 10m lakes layer is a curated set of the world's *major* lakes
(the Great Lakes included). We fetch it as GeoJSON (no geo dependencies — pure
stdlib), keep only the parts within a fixed radius of KGRR (Gerald R. Ford
Intl, Grand Rapids, MI — the default radar center), simplify each shore run
with Douglas-Peucker, and emit the result as flat polylines of lat/lon (e7).

Rings that straddle the keep-radius (e.g. Lake Michigan, most of which lies
outside the window) are split into open polylines clipped to the region so we
don't store the far shore that never renders. Small lakes fully inside the
window stay closed rings.
"""

from __future__ import annotations

import json
import math
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "include" / "data" / "water_bodies.h"
OUT_CPP = ROOT / "src" / "data" / "water_bodies_data.cpp"

# nvkelso/natural-earth-vector mirrors every Natural Earth layer as GeoJSON.
LAKES_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_lakes.geojson"
)

# Radar center = KGRR (must match config::kDefaultRadarLat/Lon).
CENTER_LAT = 42.8808
CENTER_LON = -85.5228

# "Within 100 miles" per the feature request, plus a small margin so shore runs
# reach just past the window edge (a segment crossing the boundary keeps one
# out-of-range endpoint, so the line spans the whole visible area).
KEEP_RADIUS_MI = 100.0
MI_TO_KM = 1.609344
EARTH_RADIUS_KM = 6371.0
KEEP_RADIUS_KM = KEEP_RADIUS_MI * MI_TO_KM

# Douglas-Peucker tolerance (degrees). Natural Earth 10m lakes is already sparse
# for the Great Lakes (~50 vertices across the whole window), so the tolerance is
# kept small — it only drops near-colinear points, preserving the shore shape.
SIMPLIFY_TOLERANCE_DEG = 0.0015


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    rlat1 = math.radians(lat1)
    rlat2 = math.radians(lat2)
    dlat = rlat2 - rlat1
    dlon = math.radians(lon2 - lon1)
    a = (
        math.sin(dlat / 2) ** 2
        + math.cos(rlat1) * math.cos(rlat2) * math.sin(dlon / 2) ** 2
    )
    return 2.0 * EARTH_RADIUS_KM * math.asin(math.sqrt(a))


def fetch_geojson(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "plane-radar-build"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read().decode("utf-8"))


def iter_rings(geometry: dict):
    """Yield every linear ring (exterior + interior) as a list of (lon, lat)."""
    gtype = geometry.get("type")
    coords = geometry.get("coordinates")
    if gtype == "Polygon":
        for ring in coords:
            yield ring
    elif gtype == "MultiPolygon":
        for poly in coords:
            for ring in poly:
                yield ring


# ---- Douglas-Peucker on a local planar projection --------------------------

def _perp_dist_sq(px, py, ax, ay, bx, by) -> float:
    dx = bx - ax
    dy = by - ay
    seg_sq = dx * dx + dy * dy
    if seg_sq == 0.0:
        ex = px - ax
        ey = py - ay
        return ex * ex + ey * ey
    t = ((px - ax) * dx + (py - ay) * dy) / seg_sq
    t = max(0.0, min(1.0, t))
    cx = ax + t * dx
    cy = ay + t * dy
    ex = px - cx
    ey = py - cy
    return ex * ex + ey * ey


def simplify(points: list[tuple[float, float]], tol_deg: float) -> list[tuple[float, float]]:
    """Douglas-Peucker on (lon, lat) points, longitude scaled by cos(lat)."""
    if len(points) <= 2:
        return points
    coslat = math.cos(math.radians(CENTER_LAT))
    proj = [(lon * coslat, lat) for lon, lat in points]
    tol_sq = tol_deg * tol_deg
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        lo, hi = stack.pop()
        if hi <= lo + 1:
            continue
        ax, ay = proj[lo]
        bx, by = proj[hi]
        worst = -1.0
        worst_i = -1
        for i in range(lo + 1, hi):
            px, py = proj[i]
            d = _perp_dist_sq(px, py, ax, ay, bx, by)
            if d > worst:
                worst = d
                worst_i = i
        if worst > tol_sq:
            keep[worst_i] = True
            stack.append((lo, worst_i))
            stack.append((worst_i, hi))
    return [points[i] for i, k in enumerate(keep) if k]


# ---- Clip a ring to the keep-radius window ---------------------------------

def ring_runs(ring: list[list[float]]):
    """Split a ring into runs (polylines) that lie within KEEP_RADIUS_KM.

    Returns a list of (points, closed) where points is [(lon, lat), ...].
    A ring fully inside the window is returned as one closed run; a ring that
    exits the window becomes one or more open runs, each carrying one
    out-of-range vertex on either end so it spans past the boundary.
    """
    pts = [(p[0], p[1]) for p in ring]
    # Drop the closing duplicate vertex; we treat the ring as cyclic.
    if len(pts) >= 2 and pts[0] == pts[-1]:
        pts = pts[:-1]
    n = len(pts)
    if n < 2:
        return []

    in_range = [
        haversine_km(CENTER_LAT, CENTER_LON, lat, lon) <= KEEP_RADIUS_KM
        for lon, lat in pts
    ]
    if all(in_range):
        return [(pts, True)]
    if not any(in_range):
        return []

    # Rotate so index 0 is out-of-range, then a single linear scan finds every
    # run without worrying about a run wrapping the array boundary.
    anchor = in_range.index(False)
    order = [(anchor + k) % n for k in range(n)]

    runs = []
    cur = None
    for pos, idx in enumerate(order):
        if in_range[idx]:
            if cur is None:
                prev_idx = order[pos - 1]  # out-of-range leading margin
                cur = [pts[prev_idx], pts[idx]]
            else:
                cur.append(pts[idx])
        else:
            if cur is not None:
                cur.append(pts[idx])  # out-of-range trailing margin
                runs.append((cur, False))
                cur = None
    if cur is not None:  # trailing run with no following out-of-range vertex
        cur.append(pts[order[0]])  # close with the anchor vertex
        runs.append((cur, False))
    return runs


def coord_e7(v: float) -> int:
    return int(round(v * 1e7))


def build_dataset():
    data = fetch_geojson(LAKES_URL)
    polylines = []  # list of (list[(lat_e7, lon_e7)], closed)

    for feature in data.get("features", []):
        geom = feature.get("geometry") or {}
        for ring in iter_rings(geom):
            for pts, closed in ring_runs(ring):
                simplified = simplify(pts, SIMPLIFY_TOLERANCE_DEG)
                if len(simplified) < 2:
                    continue
                e7 = [(coord_e7(lat), coord_e7(lon)) for lon, lat in simplified]
                polylines.append((e7, closed))

    # Natural Earth stores some adjacent water bodies (e.g. Saginaw Bay and Lake
    # Huron) as separate features sharing an identical boundary edge, so drop
    # runs whose exact point list we've already emitted.
    deduped = []
    seen = set()
    for e7, closed in polylines:
        key = (tuple(e7), closed)
        if key in seen:
            continue
        seen.add(key)
        deduped.append((e7, closed))

    # Longest runs first — biggest shorelines drawn first, deterministic output.
    deduped.sort(key=lambda pl: (-len(pl[0]), pl[0][0]))
    return deduped


def render_header(point_count: int, polyline_count: int) -> str:
    return "\n".join(
        [
            "// Generated by scripts/build_water_bodies.py — do not edit.",
            "#pragma once",
            "",
            "#include <cstddef>",
            "#include <cstdint>",
            "",
            "namespace data::water_bodies {",
            "",
            "struct Point {",
            "  int32_t lat_e7;",
            "  int32_t lon_e7;",
            "};",
            "",
            "// A run of shoreline. `closed` rings (small lakes fully in range)",
            "// draw a segment from the last point back to the first.",
            "struct Polyline {",
            "  uint32_t start;",
            "  uint16_t count;",
            "  uint8_t closed;",
            "};",
            "",
            f"constexpr size_t kPointCount = {point_count};",
            f"constexpr size_t kPolylineCount = {polyline_count};",
            "",
            "extern const Point kPoints[];",
            "extern const Polyline kPolylines[];",
            "",
            "}  // namespace data::water_bodies",
            "",
        ]
    )


def render_cpp(polylines) -> str:
    lines = [
        "// Generated by scripts/build_water_bodies.py — do not edit.",
        '#include "data/water_bodies.h"',
        "",
        "namespace data::water_bodies {",
        "",
        "const Point kPoints[] = {",
    ]
    start_indices = []
    idx = 0
    for e7, _closed in polylines:
        start_indices.append(idx)
        for lat_e7, lon_e7 in e7:
            lines.append(f"  {{{lat_e7}, {lon_e7}}},")
        idx += len(e7)
    lines += [
        "};",
        "",
        "const Polyline kPolylines[] = {",
    ]
    for (e7, closed), start in zip(polylines, start_indices):
        lines.append(f"  {{{start}, {len(e7)}, {1 if closed else 0}}},")
    lines += [
        "};",
        "",
        "}  // namespace data::water_bodies",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    polylines = build_dataset()
    point_count = sum(len(e7) for e7, _ in polylines)

    header = render_header(point_count, len(polylines))
    cpp = render_cpp(polylines)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(header, encoding="utf-8")
    OUT_CPP.write_text(cpp, encoding="utf-8")
    print(
        f"wrote {OUT_H.name} + {OUT_CPP.name} "
        f"({len(polylines)} polylines, {point_count} points)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
