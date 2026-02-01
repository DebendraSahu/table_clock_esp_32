"""
TTF -> VLW (TFT_eSPI smooth font) -> C FLASH array.

VLW layout, all big-endian int32:
  header  : glyphCount, version(11), fontSize, 0, ascent, descent
  glyphs  : glyphCount x (codePoint, height, width, xAdvance, dY, dX, 0)
  bitmaps : concatenated width*height bytes of 8-bit coverage

dY is the ink top measured up from the baseline; dX is the left side
bearing. TFT_eSPI plots each glyph's top edge at (cursorY + maxAscent - dY).

Header ascent/descent are set from the ACTUAL glyph extremes rather than
the face metrics: TFT_eSPI uses their sum as the line height for datum
maths, so a glyph set with no descenders must report descent 0 or every
string sits visually high.
"""

import struct
import sys
from PIL import Image, ImageDraw, ImageFont


def build(ttf_path, px_size, chars):
    font = ImageFont.truetype(ttf_path, px_size)
    pad = px_size * 2
    canvas = px_size * 4

    glyphs = []
    for ch in chars:
        img = Image.new("L", (canvas, canvas), 0)
        ImageDraw.Draw(img).text((pad, pad), ch, font=font, fill=255, anchor="ls")
        bbox = img.getbbox()
        adv = int(round(font.getlength(ch)))

        if bbox is None:  # space and friends carry no ink
            glyphs.append({"cp": ord(ch), "h": 0, "w": 0, "adv": adv,
                           "dY": 0, "dX": 0, "bmp": b""})
            continue

        x0, y0, x1, y1 = bbox
        glyphs.append({
            "cp": ord(ch),
            "h": y1 - y0,
            "w": x1 - x0,
            "adv": adv,
            "dY": pad - y0,
            "dX": x0 - pad,
            "bmp": img.crop(bbox).tobytes(),
        })

    ascent = max(g["dY"] for g in glyphs)
    descent = max(g["h"] - g["dY"] for g in glyphs)
    descent = max(descent, 0)

    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, px_size, 0, ascent, descent)
    for g in glyphs:
        out += struct.pack(">7i", g["cp"], g["h"], g["w"], g["adv"], g["dY"], g["dX"], 0)
    for g in glyphs:
        out += g["bmp"]

    return bytes(out), glyphs, ascent, descent


def to_header(blob, sym, out_path, note):
    lines = [
        "/*",
        " * --------------------------------------------------------------------",
        " * %s - anti-aliased (VLW) smooth font" % sym,
        " * --------------------------------------------------------------------",
        " * %s" % note,
        " *",
        " * VLW glyphs carry 8-bit coverage per pixel, so TFT_eSPI blends every",
        " * edge against the background colour instead of snapping it on or off.",
        " * Requires -DSMOOTH_FONT and the two-argument setTextColor(fg, bg).",
        " * VLW fonts render at their native size only - setTextSize() is ignored.",
        " * --------------------------------------------------------------------",
        " */",
        "",
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "const uint8_t %s[] PROGMEM = {" % sym,
    ]
    for i in range(0, len(blob), 16):
        lines.append("    " + "".join("0x%02X," % b for b in blob[i:i + 16]))
    lines += ["};", ""]
    open(out_path, "w", newline="\n").write("\n".join(lines))


if __name__ == "__main__":
    ttf, size, sym, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    chars = " -.:0123456789"

    blob, glyphs, asc, desc = build(ttf, size, chars)
    adv = {chr(g["cp"]): g["adv"] for g in glyphs}

    note = "Generated from %s at %dpx. Glyphs: %s" % (
        ttf.split("/")[-1], size, "".join(chars).strip())
    to_header(blob, sym, out, note)

    print("%s -> %s" % (ttf.split('/')[-1], out))
    print("  %d glyphs, %d bytes, ascent %d descent %d (line height %d)"
          % (len(glyphs), len(blob), asc, desc, asc + desc))
    print("  digit height %d, digit advance %d"
          % (next(g["h"] for g in glyphs if g["cp"] == ord("8")), adv["0"]))
    for s in ("12:35", "10:08", "9:35", "11:11"):
        print("  width of %-6s = %3d px" % (s, sum(adv[c] for c in s)))
