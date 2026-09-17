#!/usr/bin/env python3
"""Build a water-body outline dataset from OpenStreetMap via the Overpass API.

OSM gives global coverage, which the previous source (the USGS National
Hydrography Dataset) could not: NHD is US-only, so any radar center outside the
United States had no data at all. Overpass is queried over plain HTTP returning
JSON — no geo dependencies, pure stdlib — for a bounding box around the radar
center. We keep only the parts within a fixed radius, simplify each shore run
with Douglas-Peucker, and emit flat polylines of lat/lon (e7).

The center is read from config::kDefaultRadarLat/Lon rather than duplicated
here, so regenerating always matches the firmware's default location.

Two OSM shapes matter:

* `natural=water` on ways (simple lakes) and on multipolygon relations (lakes
  with islands, whose member ways arrive as unordered fragments and have to be
  stitched into rings — see stitch_rings).
* `natural=coastline`, which is open linework rather than closed polygons. A
  coastal center would otherwise render inland lakes and nothing where the sea
  is. Open runs are clipped by line_runs(), never cyclically, so the two ends
  are never joined across the map.

Unlike the old ArcGIS endpoint, Overpass does no server-side generalization, so
Douglas-Peucker carries the whole size reduction. It has no area filter either;
MIN_PERIMETER_M stands in for one server-side (see the note there) and
MIN_AREA_SQKM remains the exact gate client-side. Nor does it paginate: the
window is split into a TILE_GRID x TILE_GRID mesh and fetched a tile at a time,
because a single request covering a densely mapped region answers 504. Elements
are de-duplicated by OSM id, since anything crossing a tile seam comes back from
both tiles.

Rings that straddle the keep-radius (a large lake mostly outside the window)
are split into open polylines clipped to the region so we don't store the far
shore that never renders. Small lakes fully inside stay closed rings.

OSM data is ODbL-licensed: attribution is required, and the share-alike terms
apply to derived databases such as the generated dataset.
"""

from __future__ import annotations

import json
import math
import re
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG_H = ROOT / "include" / "config.h"
OUT_H = ROOT / "include" / "data" / "water_bodies.h"
OUT_CPP = ROOT / "src" / "data" / "water_bodies_data.cpp"

# Overpass API endpoint. Public instances are shared and rate-limited; be polite
# (honest User-Agent, a pause between requests) and expect the odd 429/504.
# https://overpass.kumi.systems/api/interpreter is a faster mirror if this one
# is struggling.
OVERPASS_URL = "https://overpass-api.de/api/interpreter"

# Per-request server-side budget.
OVERPASS_TIMEOUT_SEC = 180

# The window is fetched as a TILE_GRID x TILE_GRID mesh rather than one request.
# A 100-mile radius over a densely mapped region (the Netherlands maps every
# canal and ditch) is far too much for one query — the public instances answer
# 504 — but each tile comfortably fits. Raise this if tiles still time out.
TILE_GRID = 4

# Courtesy pause between tile requests, seconds.
TILE_DELAY_SEC = 2.0

# Backoff schedule for a throttled or timed-out tile, seconds. Overpass enforces
# a per-IP cooldown, so the first retry has to wait out the penalty rather than
# poll at it.
RETRY_DELAYS_SEC = (20.0, 60.0, 120.0)

# Server-side size gate, in metres of way length, via Overpass's length()
# evaluator. Overpass can't filter on area, but perimeter is a sound proxy: by
# the isoperimetric inequality a circle minimises perimeter for a given area, so
# anything enclosing MIN_AREA_SQKM has a perimeter of at least
# 2*sqrt(pi*A) ~= 5.0 km at A = 2 km². Cutting below that therefore cannot
# discard a waterbody the client-side area filter would have kept, and the
# margin covers relations whose members are measured individually.
#
# This is what makes a dense region tractable. Without it a single tile over the
# Netherlands returns ~17k features -- every pond and drainage ditch -- of which
# roughly 19 survive the area and radius filters.
MIN_PERIMETER_M = 4000

# Water subtypes to skip outright, on top of the size gate: these are small by
# definition and numerous. Negated regexes in Overpass also match elements with
# no `water` tag at all, so plain `natural=water` still comes through.
EXCLUDED_WATER_TYPES = (
    "pond",
    "ditch",
    "canal",
    "basin",
    "wastewater",
    "reflecting_pool",
    "swimming_pool",
    "moat",
    "fountain",
    "fish_pass",
)

def config_default_center() -> tuple[float, float]:
    """Read kDefaultRadarLat / kDefaultRadarLon out of include/config.h.

    Parsed rather than copied so the dataset is always built for the center the
    firmware actually boots with. A stale duplicate here fails silently: the
    outlines simply don't line up with anything on screen.
    """
    text = CONFIG_H.read_text(encoding="utf-8")
    found = {}
    for name in ("kDefaultRadarLat", "kDefaultRadarLon"):
        match = re.search(
            r"constexpr\s+double\s+" + name + r"\s*=\s*(-?\d+(?:\.\d+)?)\s*;", text
        )
        if match is None:
            raise SystemExit(f"{CONFIG_H}: could not find {name}")
        found[name] = float(match.group(1))
    return found["kDefaultRadarLat"], found["kDefaultRadarLon"]


# Radar center, taken from config::kDefaultRadarLat/Lon.
CENTER_LAT, CENTER_LON = config_default_center()

# "Within 100 miles" per the feature request, plus a small margin so shore runs
# reach just past the window edge (a segment crossing the boundary keeps one
# out-of-range endpoint, so the line spans the whole visible area).
KEEP_RADIUS_MI = 100.0
MI_TO_KM = 1.609344
EARTH_RADIUS_KM = 6371.0
KEEP_RADIUS_KM = KEEP_RADIUS_MI * MI_TO_KM

# Drop lakes smaller than this. At the ~0.28 mi/px scale of a 100-mile radar,
# anything much below a few km² is barely a couple of pixels across, so this
# both keeps the dataset small and avoids sub-pixel clutter. Tune to taste.
MIN_AREA_SQKM = 2.0

# Douglas-Peucker tolerance (degrees). OSM geometry arrives at full resolution
# (Overpass has no server-side generalization), so this carries the whole load
# of shrinking the embedded dataset; it drops near-colinear points while
# preserving the shore shape.
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


def keep_bbox() -> tuple[float, float, float, float]:
    """Lon/lat envelope enclosing the keep-radius, with a 10% margin."""
    km_per_deg_lat = EARTH_RADIUS_KM * math.pi / 180.0
    dlat = KEEP_RADIUS_KM / km_per_deg_lat * 1.1
    dlon = dlat / max(math.cos(math.radians(CENTER_LAT)), 1e-6)
    return (CENTER_LON - dlon, CENTER_LAT - dlat,
            CENTER_LON + dlon, CENTER_LAT + dlat)


def tile_query(south: float, west: float, north: float, east: float) -> str:
    """Overpass QL for water polygons + coastline across one tile.

    Coastline is fetched alongside the waterbodies because a sea shore is not a
    closed polygon in OSM: `natural=coastline` is open linework, which suits a
    radar overlay that draws polylines anyway. Without it, a coastal center
    would render inland lakes and nothing where the sea is.
    """
    bbox = f"{south:.6f},{west:.6f},{north:.6f},{east:.6f}"
    skip = "|".join(EXCLUDED_WATER_TYPES)
    notsmall = f'["water"!~"^({skip})$"]'
    big = f"(if: length() > {MIN_PERIMETER_M})"
    return (
        f"[out:json][timeout:{OVERPASS_TIMEOUT_SEC}];\n"
        "(\n"
        f'  way["natural"="water"]{notsmall}({bbox}){big};\n'
        f'  relation["natural"="water"]["type"="multipolygon"]{notsmall}'
        f"({bbox}){big};\n"
        f'  way["natural"="coastline"]({bbox});\n'
        ");\n"
        "out geom;\n"
    )


def _post_overpass(query: str) -> dict:
    req = urllib.request.Request(
        OVERPASS_URL,
        data=query.encode("utf-8"),
        headers={
            "User-Agent": "plane-radar-build (ESP32-Plane-Radar)",
            "Content-Type": "text/plain; charset=utf-8",
        },
    )
    with urllib.request.urlopen(req, timeout=OVERPASS_TIMEOUT_SEC + 60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def fetch_overpass_elements() -> list[dict]:
    """Fetch the window tile by tile, de-duplicated by OSM id.

    A way or relation straddling a tile seam comes back from every tile it
    touches, with identical geometry each time, so keying on (type, id) keeps
    exactly one copy and leaves the geometry untouched.
    """
    xmin, ymin, xmax, ymax = keep_bbox()
    lon_step = (xmax - xmin) / TILE_GRID
    lat_step = (ymax - ymin) / TILE_GRID

    by_id: dict[tuple[str, int], dict] = {}
    total = TILE_GRID * TILE_GRID
    for row in range(TILE_GRID):
        for col in range(TILE_GRID):
            n = row * TILE_GRID + col + 1
            south = ymin + row * lat_step
            north = south + lat_step
            west = xmin + col * lon_step
            east = west + lon_step
            query = tile_query(south, west, north, east)

            payload = None
            for attempt in range(len(RETRY_DELAYS_SEC) + 1):
                try:
                    payload = _post_overpass(query)
                    break
                except urllib.error.HTTPError as exc:
                    retryable = exc.code in (429, 502, 503, 504)
                    if not retryable or attempt == len(RETRY_DELAYS_SEC):
                        raise SystemExit(
                            f"Overpass tile {n}/{total} failed: "
                            f"HTTP {exc.code} {exc.reason}.\n"
                            "Public instances are shared and throttled — retry "
                            "later, raise TILE_GRID to ask for less at a time, "
                            "or point OVERPASS_URL at another mirror."
                        ) from exc
                    delay = RETRY_DELAYS_SEC[attempt]
                    print(f"  tile {n}/{total}: HTTP {exc.code}, "
                          f"retrying in {delay:.0f}s")
                    time.sleep(delay)

            found = payload.get("elements", []) if payload else []
            for el in found:
                key = (el.get("type"), el.get("id"))
                if key not in by_id:
                    by_id[key] = el
            print(f"  tile {n}/{total}: {len(found)} elements "
                  f"({len(by_id)} unique so far)")
            if n < total:
                time.sleep(TILE_DELAY_SEC)

    if not by_id:
        raise SystemExit(
            f"Overpass returned no water within {KEEP_RADIUS_MI:.0f} mi of "
            f"({CENTER_LAT}, {CENTER_LON}).\n"
            f"That center comes from config::kDefaultRadarLat/Lon in "
            f"{CONFIG_H.relative_to(ROOT)}."
        )
    return list(by_id.values())


def _geom_points(geometry) -> list[tuple[float, float]]:
    """Overpass `out geom;` node list -> [(lon, lat), ...]."""
    return [(p["lon"], p["lat"]) for p in geometry or []]


def stitch_rings(segments: list[list[tuple[float, float]]]):
    """Join multipolygon member ways end-to-end.

    OSM stores a lake with islands as a relation whose member ways are arbitrary
    fragments in arbitrary order and direction, so they have to be walked into
    rings before anything downstream can treat them as shorelines. Shared nodes
    come back with byte-identical coordinates, so exact matching is safe here.

    Yields (points, closed). A fragment that never closes is still real shore
    (the relation may extend past the query window), so it is kept as an open
    run rather than discarded.
    """
    pending = [list(s) for s in segments if len(s) >= 2]
    while pending:
        cur = pending.pop()
        joined = True
        while joined and cur[0] != cur[-1]:
            joined = False
            for i, seg in enumerate(pending):
                if seg[0] == cur[-1]:
                    cur.extend(seg[1:])
                elif seg[-1] == cur[-1]:
                    cur.extend(reversed(seg[:-1]))
                elif seg[-1] == cur[0]:
                    cur[:0] = seg[:-1]
                elif seg[0] == cur[0]:
                    cur[:0] = list(reversed(seg[1:]))
                else:
                    continue
                pending.pop(i)
                joined = True
                break
        closed = cur[0] == cur[-1] and len(cur) >= 4
        if len(cur) >= 2:
            yield cur, closed


def ring_area_sqkm(ring: list[tuple[float, float]]) -> float:
    """Shoelace area on a local equirectangular projection."""
    if len(ring) < 3:
        return 0.0
    lat0 = sum(lat for _, lat in ring) / len(ring)
    deg_km = EARTH_RADIUS_KM * math.pi / 180.0
    kx = deg_km * math.cos(math.radians(lat0))
    total = 0.0
    for i in range(len(ring)):
        lon1, lat1 = ring[i]
        lon2, lat2 = ring[(i + 1) % len(ring)]
        total += (lon1 * kx) * (lat2 * deg_km) - (lon2 * kx) * (lat1 * deg_km)
    return abs(total) / 2.0


def iter_shorelines(elements: list[dict]):
    """Yield (points, closed) shorelines from raw Overpass elements.

    Closed water rings below MIN_AREA_SQKM are dropped here; Overpass has no
    server-side area filter, so the cut that NHD did in its WHERE clause has to
    happen client-side.
    """
    for el in elements:
        tags = el.get("tags") or {}
        kind = el.get("type")

        if kind == "way" and tags.get("natural") == "coastline":
            pts = _geom_points(el.get("geometry"))
            if len(pts) >= 2:
                yield pts, False  # open linework by definition
            continue

        if tags.get("natural") != "water":
            continue

        if kind == "way":
            pts = _geom_points(el.get("geometry"))
            if len(pts) < 2:
                continue
            closed = pts[0] == pts[-1]
            if closed and ring_area_sqkm(pts) < MIN_AREA_SQKM:
                continue
            yield pts, closed
        elif kind == "relation":
            segments = [
                _geom_points(m.get("geometry"))
                for m in el.get("members") or []
                if m.get("type") == "way" and m.get("geometry")
            ]
            for pts, closed in stitch_rings(segments):
                if closed and ring_area_sqkm(pts) < MIN_AREA_SQKM:
                    continue
                yield pts, closed


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


def line_runs(line: list[tuple[float, float]]):
    """Clip an open polyline to the keep-radius window.

    ring_runs() treats its input as cyclic, which is wrong for coastline and for
    relation fragments that never closed: joining their two ends would draw a
    shoreline straight across the map. This walks the line once, keeping one
    out-of-range vertex on each side of a run so the stroke reaches past the
    window edge like the ring clipper does.
    """
    pts = [(p[0], p[1]) for p in line]
    if len(pts) < 2:
        return []

    in_range = [
        haversine_km(CENTER_LAT, CENTER_LON, lat, lon) <= KEEP_RADIUS_KM
        for lon, lat in pts
    ]
    if all(in_range):
        return [(pts, False)]
    if not any(in_range):
        return []

    runs = []
    cur = None
    for i, inside in enumerate(in_range):
        if inside:
            if cur is None:
                cur = [pts[i - 1], pts[i]] if i > 0 else [pts[i]]
            else:
                cur.append(pts[i])
        elif cur is not None:
            cur.append(pts[i])  # out-of-range trailing margin
            runs.append((cur, False))
            cur = None
    if cur is not None:
        runs.append((cur, False))
    return runs


def coord_e7(v: float) -> int:
    return int(round(v * 1e7))


def build_dataset():
    elements = fetch_overpass_elements()
    polylines = []  # list of (list[(lat_e7, lon_e7)], closed)

    for shore, closed in iter_shorelines(elements):
        # Closed rings clip cyclically; open runs must not wrap end-to-start.
        runs = ring_runs(shore) if closed else line_runs(shore)
        for pts, run_closed in runs:
            simplified = simplify(pts, SIMPLIFY_TOLERANCE_DEG)
            if len(simplified) < 2:
                continue
            e7 = [(coord_e7(lat), coord_e7(lon)) for lon, lat in simplified]
            polylines.append((e7, run_closed))

    # Adjacent waterbodies can share an identical boundary edge and some rings
    # survive clipping identically, so drop runs whose exact point list we've
    # already emitted.
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


# Attribution carried by both generated files. ODbL requires the credit to
# travel with the data, so it has to live in the emitted source rather than only
# in this script -- the script is not what gets distributed or compiled.
ATTRIBUTION_LINES = (
    "// Generated by scripts/build_water_bodies.py — do not edit.",
    "//",
    "// Contains information from OpenStreetMap, which is made available under",
    "// the Open Database License (ODbL) v1.0.",
    "// © OpenStreetMap contributors — https://www.openstreetmap.org/copyright",
    "//",
    "// This file is a database derived from OSM data: keep this notice with it,",
    "// and carry the attribution into anything built from it.",
)


def render_header(point_count: int, polyline_count: int) -> str:
    return "\n".join(
        [
            *ATTRIBUTION_LINES,
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
        *ATTRIBUTION_LINES,
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
    print(
        f"center {CENTER_LAT}, {CENTER_LON} "
        f"(config::kDefaultRadarLat/Lon), keep radius {KEEP_RADIUS_MI:.0f} mi, "
        f"{TILE_GRID}x{TILE_GRID} tiles"
    )
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
