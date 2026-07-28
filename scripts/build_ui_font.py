#!/usr/bin/env python3
"""Generate the embedded anti-aliased UI fonts (VLW, TFT_eSPI / LovyanGFX format).

Two kinds of output are produced from one TrueType file:

  * data/ui_font.vlw  — the "master" font, rendered at 45 px native (3x the
    15 px baseline). Used by the 240 px C3 build for everything, and by the
    720 px Qualia build for the boot/status screens. It is only ever
    downscaled at runtime (crisp); see config::kVlwNativeSizeScale.

  * data/ui_font_<H>.vlw — one font per exact on-screen pixel height H used by
    the 720 px Qualia radar (cardinals, range label, tags, runways, clock).
    LovyanGFX's VLW scaler is nearest-neighbour (no interpolation), so drawing
    each label from a font rendered natively at its target height is crisper
    than up/down-scaling a single master. EM_PX is solved per height so the
    font's reported height (ascent+descent) matches H; the firmware then draws
    at ~1.0x. See displayFontApplyHeight() in hardware/display_font.cpp.

Charset: ASCII 33..126 plus U+00B0 (degree sign) — 95 glyphs. Space (0x20) is
intentionally omitted (LovyanGFX derives it).

Usage (regenerate everything the firmware embeds):
    python scripts/build_ui_font.py assets/fonts/NotoSans-Regular.ttf \
        --out-dir data --master-em 45 --heights 14,16,17,18,20,21,42,69

Requires Pillow (PIL) with FreeType support.
"""
import argparse
import os
import struct

from PIL import Image, ImageDraw, ImageFont

CHARSET = list(range(33, 127)) + [0xB0]


def build(ttf_path: str, em_px: int) -> tuple[bytes, int]:
    """Return (vlw_bytes, font_height) for the font rendered at em_px."""
    font = ImageFont.truetype(ttf_path, em_px)
    ascent, descent = font.getmetrics()
    pad = em_px
    canvas_w, canvas_h = em_px * 3, ascent + descent + 2 * pad
    baseline_y, pen_x = pad + ascent, pad

    glyphs = []  # (codepoint, height, width, xAdvance, dY, gdX, bitmap)
    for cp in CHARSET:
        img = Image.new("L", (canvas_w, canvas_h), 0)
        ImageDraw.Draw(img).text((pen_x, baseline_y), chr(cp), fill=255,
                                 font=font, anchor="ls")
        advance = round(font.getlength(chr(cp)))
        bbox = img.getbbox()
        if bbox is None:  # blank glyph
            glyphs.append((cp, 0, 0, advance, 0, 0, b""))
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        glyphs.append((cp, h, w, advance, baseline_y - y0, x0 - pen_x,
                       img.crop(bbox).tobytes()))

    # int8/uint8/int16 field limits enforced by the VLW readers.
    assert all(g[2] < 256 and g[1] < 256 and g[3] < 256 for g in glyphs)
    assert all(-128 <= g[5] < 128 for g in glyphs)

    hdr_ascent = max(g[4] for g in glyphs)
    hdr_descent = max(g[1] - g[4] for g in glyphs)

    out = bytearray()
    # Header (big-endian): count, version, fontSize, mboxY(unused), ascent, descent
    out += struct.pack(">IIIIII", len(glyphs), 11, em_px, 0, hdr_ascent, hdr_descent)
    # Per-glyph metrics: unicode, height, width, xAdvance, dY, gdX, pad
    for cp, h, w, adv, dY, gdX, _ in glyphs:
        out += struct.pack(">iiiiiii", cp, h, w, adv, dY, gdX, 0)
    # Bitmaps (8-bit alpha, row-major), in glyph order
    for *_, bmp in glyphs:
        out += bmp
    # LovyanGFX reports fontHeight as ascent + descent (the header values).
    return bytes(out), hdr_ascent + hdr_descent


def solve_em_for_height(ttf_path: str, target_h: int) -> tuple[int, bytes, int]:
    """Find the EM_PX whose rendered font-height is closest to target_h.

    Height grows monotonically with EM_PX (~1.03x), so scan a small window and
    prefer the smallest EM_PX whose height is >= target (residual downscale, not
    a blocky upscale)."""
    best = None  # (abs_err, height>=target, em, bytes, height)
    lo = max(6, target_h - 6)
    for em in range(lo, target_h + 7):
        data, h = build(ttf_path, em)
        key = (abs(h - target_h), 0 if h >= target_h else 1)
        if best is None or key < best[0]:
            best = (key, em, data, h)
    _, em, data, h = best
    return em, data, h


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ttf", help="path to a TrueType font (e.g. assets/fonts/NotoSans-Regular.ttf)")
    ap.add_argument("--out-dir", default="data", help="output directory (default: data)")
    ap.add_argument("--master-em", type=int, default=45,
                    help="EM_PX for the master ui_font.vlw (default: 45)")
    ap.add_argument("--heights", default="",
                    help="comma-separated exact on-screen heights for the per-size set "
                         "(e.g. 14,16,17,18,20,21,42,69); empty = master only")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    master, mh = build(args.ttf, args.master_em)
    master_path = os.path.join(args.out_dir, "ui_font.vlw")
    with open(master_path, "wb") as f:
        f.write(master)
    print(f"wrote {master_path}: {len(master)} bytes, {len(CHARSET)} glyphs "
          f"@ EM_PX={args.master_em} (height {mh})")

    if args.heights.strip():
        for target in [int(x) for x in args.heights.split(",") if x.strip()]:
            em, data, h = solve_em_for_height(args.ttf, target)
            path = os.path.join(args.out_dir, f"ui_font_{target}.vlw")
            with open(path, "wb") as f:
                f.write(data)
            print(f"wrote {path}: {len(data)} bytes @ EM_PX={em} "
                  f"(height {h}, target {target})")


if __name__ == "__main__":
    main()
