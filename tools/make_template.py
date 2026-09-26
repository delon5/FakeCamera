#!/usr/bin/env python3
"""Turn any picture into a FakeCamera image.

FakeCamera reads uncompressed BMP files from ux0:data/FakeCamera. This script
resizes a JPG/PNG/whatever into a 24 bits BMP of the wanted size, either named
for a title (TITLEID.bmp, TITLEID_Front.bmp, TITLEID_Back.bmp) or with a free
name to reference from config.txt.

Examples:
  make_template.py me.jpg --name face.bmp
  make_template.py me.jpg --title PCSF00214 --camera front
  make_template.py room.jpg --title PCSA00010 --size 1280x720 --fit cover
  make_template.py card.png --name ar-card.bmp --fit contain --background white

Needs Pillow:  pip install pillow
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("This script needs Pillow: pip install pillow")


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def fit_image(im, size, mode, background):
    im = im.convert("RGB")
    w, h = size
    if mode == "stretch":
        return im.resize((w, h), Image.LANCZOS)
    scale = (max if mode == "cover" else min)(w / im.width, h / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), background)
    canvas.paste(im, ((w - nw) // 2, (h - nh) // 2))
    return canvas


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="source picture (any format Pillow reads)")
    ap.add_argument("--title", action="append", default=[], help="title ID to name the file after (repeatable)")
    ap.add_argument("--camera", choices=["both", "front", "back"], default="both", help="which camera the picture is for (with --title)")
    ap.add_argument("--name", help="free file name instead of a title name, to use with image=/front=/back= in config.txt")
    ap.add_argument("--size", default="640x480", help="output size, default 640x480 (the largest camera resolution)")
    ap.add_argument("--fit", choices=["cover", "contain", "stretch"], default="cover", help="cover: fill and crop (default), contain: letterbox, stretch: distort")
    ap.add_argument("--background", default="black", help="letterbox colour for --fit contain")
    ap.add_argument("--out", default=".", help="output directory (copy its content to ux0:data/FakeCamera)")
    args = ap.parse_args()

    if not args.title and not args.name:
        ap.error("give --title TITLEID (repeatable) or --name file.bmp")

    im = fit_image(Image.open(args.input), parse_size(args.size), args.fit, args.background)
    os.makedirs(args.out, exist_ok=True)

    names = []
    if args.name:
        names.append(args.name if args.name.lower().endswith(".bmp") else args.name + ".bmp")
    suffix = {"both": "", "front": "_Front", "back": "_Back"}[args.camera]
    names += [f"{title.upper()}{suffix}.bmp" for title in args.title]

    for name in names:
        path = os.path.join(args.out, name)
        im.save(path, format="BMP")
        print(f"wrote {path} ({im.width}x{im.height})")


if __name__ == "__main__":
    main()
