#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
from pathlib import Path


MAGIC = 0x474D4956  # VIMG, little-endian on disk
VERSION = 1
LV_IMG_CF_TRUE_COLOR_ALPHA = 5
WIDTH_LIMIT = 240
HEIGHT_LIMIT = 240


def read_bmp32(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path} is not a BMP file")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path} has unsupported BMP DIB header")
    width, height_signed, planes, bpp, compression, image_size = struct.unpack_from("<iiHHII", data, 18)
    if planes != 1 or bpp != 32:
        raise ValueError(f"{path} must be 32-bit BMP, got planes={planes} bpp={bpp}")
    if width <= 0 or abs(height_signed) <= 0 or width > WIDTH_LIMIT or abs(height_signed) > HEIGHT_LIMIT:
        raise ValueError(f"{path} dimensions are unsupported: {width}x{height_signed}")
    height = abs(height_signed)
    top_down = height_signed < 0
    stride = width * 4
    if pixel_offset + stride * height > len(data):
        raise ValueError(f"{path} pixel data is truncated")

    pixels = bytearray(width * height * 3)
    for y in range(height):
        src_y = y if top_down else height - 1 - y
        row = pixel_offset + src_y * stride
        for x in range(width):
            b, g, r, a = data[row + x * 4 : row + x * 4 + 4]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            dst = (y * width + x) * 3
            pixels[dst] = rgb565 & 0xFF
            pixels[dst + 1] = (rgb565 >> 8) & 0xFF
            pixels[dst + 2] = a
    return width, height, bytes(pixels)


def convert_png(png_path: Path, out_path: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="vibeboard_bmp_") as temp_dir:
        bmp_path = Path(temp_dir) / f"{png_path.stem}.bmp"
        subprocess.run(
            ["sips", "-s", "format", "bmp", str(png_path), "--out", str(bmp_path)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        width, height, pixels = read_bmp32(bmp_path)
    header = struct.pack("<IHHHHI", MAGIC, VERSION, width, height, LV_IMG_CF_TRUE_COLOR_ALPHA, len(pixels))
    out_path.write_bytes(header + pixels)


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert weather PNG assets to VibeBoard LVGL raw image bins.")
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--out-dir", type=Path)
    args = parser.parse_args()

    for src in args.paths:
        if src.is_dir():
            pngs = sorted(src.glob("*.png"))
        else:
            pngs = [src]
        for png in pngs:
            out_dir = args.out_dir or png.parent
            out_dir.mkdir(parents=True, exist_ok=True)
            out = out_dir / f"{png.stem}.bin"
            convert_png(png, out)
            print(f"{png} -> {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
