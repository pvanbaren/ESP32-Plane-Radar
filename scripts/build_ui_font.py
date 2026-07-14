#!/usr/bin/env python3
"""Generate data/ui_font.vlw — the embedded anti-aliased UI font.

Produces a VLW smooth font (TFT_eSPI / LovyanGFX format) from a TrueType file.
The font is rendered at 45 px native (3x the original 15 px) so it stays crisp
when downscaled on both the 240 px (C3) and 720 px (Qualia S3) builds; see
config::kVlwNativeSizeScale.

Charset: ASCII 33..126 plus U+00B0 (degree sign) — 95 glyphs, matching the
original font. Space (0x20) is intentionally omitted (LovyanGFX derives it).

Usage:
    python scripts/build_ui_font.py NotoSans-Regular.ttf [-o data/ui_font.vlw]

Requires Pillow (PIL) with FreeType support.
"""
import argparse
import struct

from PIL import Image, ImageDraw, ImageFont

EM_PX = 45  # native render size (== 3 x the 15 px baseline; see kVlwNativeSizeScale)
CHARSET = list(range(33, 127)) + [0xB0]


def build(ttf_path: str) -> bytes:
    font = ImageFont.truetype(ttf_path, EM_PX)
    ascent, descent = font.getmetrics()
    pad = EM_PX
    canvas_w, canvas_h = EM_PX * 3, ascent + descent + 2 * pad
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
    out += struct.pack(">IIIIII", len(glyphs), 11, EM_PX, 0, hdr_ascent, hdr_descent)
    # Per-glyph metrics: unicode, height, width, xAdvance, dY, gdX, pad
    for cp, h, w, adv, dY, gdX, _ in glyphs:
        out += struct.pack(">iiiiiii", cp, h, w, adv, dY, gdX, 0)
    # Bitmaps (8-bit alpha, row-major), in glyph order
    for *_, bmp in glyphs:
        out += bmp
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ttf", help="path to a TrueType font (e.g. NotoSans-Regular.ttf)")
    ap.add_argument("-o", "--out", default="data/ui_font.vlw")
    args = ap.parse_args()
    data = build(args.ttf)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"wrote {args.out}: {len(data)} bytes, {len(CHARSET)} glyphs @ {EM_PX}px")


if __name__ == "__main__":
    main()
