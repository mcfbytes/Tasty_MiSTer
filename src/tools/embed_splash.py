#!/usr/bin/env python3
"""Encode a PNG as a zlib blob for TastySplash (letterboxed at blit)."""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    i = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while i + 12 <= len(data):
        (n,) = struct.unpack(">I", data[i : i + 4])
        kind = data[i + 4 : i + 8]
        chunk = data[i + 8 : i + 8 + n]
        i += 12 + n
        if kind == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, inter = struct.unpack(
                ">IIBBBBB", chunk
            )
            if bit_depth != 8 or inter != 0 or comp != 0:
                raise ValueError("need 8-bit non-interlaced PNG")
        elif kind == b"IDAT":
            idat.extend(chunk)
        elif kind == b"IEND":
            break
    if width is None or color_type is None:
        raise ValueError("missing IHDR")
    raw = zlib.decompress(bytes(idat))
    if color_type == 6:
        bpp = 4
    elif color_type == 2:
        bpp = 3
    else:
        raise ValueError(f"unsupported color type {color_type}")
    stride = width * bpp
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ftype = raw[pos]
        pos += 1
        row = bytearray(raw[pos : pos + stride])
        pos += stride
        if ftype == 1:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + left) & 255
        elif ftype == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif ftype == 3:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif ftype == 4:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                up = prev[x]
                ul = prev[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + paeth(left, up, ul)) & 255
        elif ftype != 0:
            raise ValueError(f"filter {ftype}")
        dst = y * width * 4
        if bpp == 4:
            out[dst : dst + stride] = row
        else:
            for x in range(width):
                o = dst + x * 4
                s = x * 3
                out[o : o + 3] = row[s : s + 3]
                out[o + 3] = 255
        prev = row
    return width, height, bytes(out)


def write_header(png: Path, dest: Path) -> None:
    w, h, rgba = decode_png(png.read_bytes())
    blob = zlib.compress(rgba, 9)
    body = ",\n".join(
        ",".join(f"0x{blob[i + j]:02x}" for j in range(min(16, len(blob) - i)))
        for i in range(0, len(blob), 16)
    )
    dest.write_text(
        f"// SPDX-License-Identifier: GPL-3.0-or-later\n"
        f"#pragma once\n"
        f"#include <cstddef>\n"
        f"#include <cstdint>\n"
        f"inline constexpr std::uint32_t kSplashEmbedW = {w}u;\n"
        f"inline constexpr std::uint32_t kSplashEmbedH = {h}u;\n"
        f"inline constexpr std::size_t kSplashEmbedRaw = {len(rgba)}u;\n"
        f"inline constexpr std::size_t kSplashEmbedZ = {len(blob)}u;\n"
        f"inline constexpr unsigned char kSplashEmbed[] = {{\n{body}\n}};\n"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("header")
    args = ap.parse_args()
    write_header(Path(args.png), Path(args.header))
    return 0


if __name__ == "__main__":
    sys.exit(main())
