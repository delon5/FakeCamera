#!/usr/bin/env python3
"""Draws an EAN-13 or EAN-8 barcode (JAN code) as a FakeCamera picture.

Ciel nosurge creates a different Sharl for each barcode scanned with the back
camera: make a picture of the product code you want and point the title's
section of config.txt at it.

  make_barcode.py 4901234567894                    # EAN-13 (12 digits: check digit added)
  make_barcode.py 49012347 --out sharl2.bmp        # EAN-8
  make_barcode.py 4901234567894 --size 320x240

Needs Pillow (pip install pillow).
"""
import argparse
import os
import sys

L_CODES = ["0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011"]
G_CODES = ["0100111", "0110011", "0011011", "0100001", "0011101", "0111001", "0000101", "0010001", "0001001", "0010111"]
R_CODES = ["1110010", "1100110", "1101100", "1000010", "1011100", "1001110", "1010000", "1000100", "1001000", "1110100"]
PARITY = ["LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG", "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"]


def check_digit(digits):
    """EAN check digit of the digits before it (any length)."""
    total = 0
    for i, d in enumerate(reversed(digits)):
        total += int(d) * (3 if i % 2 == 0 else 1)
    return str((10 - total % 10) % 10)


def normalize(number):
    digits = "".join(ch for ch in number if ch.isdigit())
    if len(digits) in (7, 12):
        digits += check_digit(digits)
    if len(digits) not in (8, 13):
        sys.exit("give 12 or 13 digits for EAN-13, 7 or 8 for EAN-8")
    if check_digit(digits[:-1]) != digits[-1]:
        sys.exit(f"wrong check digit: {digits[:-1]} should end with {check_digit(digits[:-1])}")
    return digits


def modules(digits):
    """The bar pattern as a string of '1' (bar) and '0' (space)."""
    if len(digits) == 13:
        parity = PARITY[int(digits[0])]
        left = "".join((L_CODES if parity[i] == "L" else G_CODES)[int(d)] for i, d in enumerate(digits[1:7]))
        right = "".join(R_CODES[int(d)] for d in digits[7:])
    else:
        left = "".join(L_CODES[int(d)] for d in digits[:4])
        right = "".join(R_CODES[int(d)] for d in digits[4:])
    return "101" + left + "01010" + right + "101"


def draw_barcode(number, size=(640, 480)):
    from PIL import Image, ImageDraw, ImageFont
    digits = normalize(number)
    pattern = modules(digits)
    w, h = size
    quiet = 10
    scale = max(1, (w - 40) // (len(pattern) + 2 * quiet))
    bar_h = int(h * 0.5)
    x0 = (w - len(pattern) * scale) // 2
    y0 = int(h * 0.2)
    im = Image.new("RGB", size, (255, 255, 255))
    d = ImageDraw.Draw(im)
    # guard bars (start, centre, end) reach lower than the digit bars
    guards = set(range(0, 3)) | set(range(len(pattern) - 3, len(pattern)))
    mid = len(pattern) // 2
    guards |= set(range(mid - 2, mid + 3))
    for i, m in enumerate(pattern):
        if m == "1":
            extra = int(h * 0.04) if i in guards else 0
            d.rectangle([x0 + i * scale, y0, x0 + (i + 1) * scale - 1, y0 + bar_h + extra], fill=(0, 0, 0))
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", int(h * 0.07))
    except OSError:
        font = ImageFont.load_default(size=int(h * 0.07))
    if len(digits) == 13:
        groups = [(digits[0], x0 - int(h * 0.07)), (digits[1:7], x0 + 3 * scale), (digits[7:], x0 + 50 * scale)]
    else:
        groups = [(digits[:4], x0 + 3 * scale), (digits[4:], x0 + 36 * scale)]
    for text, x in groups:
        d.text((x + scale, y0 + bar_h + int(h * 0.045)), " ".join(text), font=font, fill=(0, 0, 0))
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("number", help="EAN-13 (12 or 13 digits) or EAN-8 (7 or 8 digits)")
    ap.add_argument("--out", default=None, help="output BMP (default barcode-<number>.bmp)")
    ap.add_argument("--size", default="640x480")
    args = ap.parse_args()
    w, h = (int(v) for v in args.size.lower().split("x"))
    im = draw_barcode(args.number, (w, h))
    out = args.out or f"barcode-{normalize(args.number)}.bmp"
    im.save(out, format="BMP")
    print(f"wrote {out} ({im.width}x{im.height}, code {normalize(args.number)})")


if __name__ == "__main__":
    main()
