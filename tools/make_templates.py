#!/usr/bin/env python3
"""Generates the ready-made pictures of the templates/ directory.

Every picture is drawn from scratch (no copyrighted artwork): a synthetic
face for face detection, a colourful pattern, coloured objects, a room
panorama, an outdoor backdrop, AR Play markers, a QR code, a small avatar and
a white frame. Run it from the repository root:

  python3 tools/make_templates.py

Needs Pillow (pip install pillow) and, for the QR code, qrcode (pip install qrcode).
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "templates")

# The six PlayStation AR Play cards are 5x5 grids: a black frame around a 3x3
# code ('#' black, '.' white). The code alone is what the AR tracking reads.
AR_CARDS = {
    1: ["#####", "###.#", "#.#.#", "#..##", "#####"],
    2: ["#####", "#####", "#...#", "###.#", "#####"],
    3: ["#####", "#####", "#.#.#", "##.##", "#####"],
    4: ["#####", "#.#.#", "#...#", "#...#", "#####"],
    5: ["#####", "#.###", "#...#", "#.#.#", "#####"],
    6: ["#####", "###.#", "#.###", "#.#.#", "#####"],
}


def font(size, bold=True):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def save(im, name):
    path = os.path.join(OUT, name)
    im.convert("RGB").save(path, format="BMP")
    print(f"{name}: {im.width}x{im.height}, {os.path.getsize(path) // 1024} KB")


def vertical_gradient(size, top, bottom):
    w, h = size
    im = Image.new("RGB", size)
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return im


# ---------------------------------------------------------------------------

def make_face(size=(640, 480)):
    """A frontal face with the light/dark structure face detectors look for."""
    w, h = size
    im = vertical_gradient(size, (196, 208, 222), (150, 164, 184))
    d = ImageDraw.Draw(im)
    cx, cy = w // 2, int(h * 0.47)
    # shoulders and neck
    d.rounded_rectangle([cx - 210, h - 130, cx + 210, h + 80], radius=90, fill=(58, 74, 112))
    skin, shade, dark = (226, 178, 148), (196, 142, 112), (60, 40, 32)
    d.rectangle([cx - 46, cy + 120, cx + 46, h - 100], fill=shade)
    # hair behind the head, then the head
    d.ellipse([cx - 150, cy - 190, cx + 150, cy + 60], fill=(54, 36, 26))
    d.ellipse([cx - 118, cy - 156, cx + 118, cy + 156], fill=skin)
    d.ellipse([cx - 112, cy - 150, cx + 112, cy + 120], fill=(236, 190, 160))
    # ears
    for sx in (-1, 1):
        d.ellipse([cx + sx * 130 - 22, cy - 26, cx + sx * 130 + 22, cy + 34], fill=skin, outline=shade)
    # hair fringe
    d.chord([cx - 122, cy - 170, cx + 122, cy - 20], 190, 350, fill=(54, 36, 26))
    # eyebrows
    for sx in (-1, 1):
        d.line([(cx + sx * 80, cy - 62), (cx + sx * 30, cy - 70)], fill=dark, width=9)
    # eyes: white, iris, pupil, highlight
    for sx in (-1, 1):
        ex = cx + sx * 52
        d.ellipse([ex - 30, cy - 46, ex + 30, cy - 12], fill=(250, 250, 250), outline=(120, 90, 80))
        d.ellipse([ex - 14, cy - 43, ex + 14, cy - 15], fill=(70, 100, 130))
        d.ellipse([ex - 7, cy - 36, ex + 7, cy - 22], fill=(10, 10, 12))
        d.ellipse([ex - 1, cy - 38, ex + 5, cy - 32], fill=(255, 255, 255))
        d.line([(ex - 30, cy - 46), (ex + 30, cy - 46)], fill=(70, 50, 40), width=3)
    # nose: bright bridge, shaded sides and nostrils
    d.polygon([(cx, cy - 20), (cx - 22, cy + 34), (cx + 22, cy + 34)], fill=(232, 186, 156))
    d.line([(cx - 20, cy + 30), (cx - 8, cy + 40)], fill=shade, width=5)
    d.line([(cx + 20, cy + 30), (cx + 8, cy + 40)], fill=shade, width=5)
    d.ellipse([cx - 18, cy + 30, cx - 6, cy + 40], fill=(180, 120, 100))
    d.ellipse([cx + 6, cy + 30, cx + 18, cy + 40], fill=(180, 120, 100))
    # cheeks and mouth
    for sx in (-1, 1):
        d.ellipse([cx + sx * 78 - 22, cy + 20, cx + sx * 78 + 22, cy + 50], fill=(232, 168, 150))
    d.chord([cx - 58, cy + 40, cx + 58, cy + 104], 15, 165, fill=(150, 60, 60))
    d.chord([cx - 50, cy + 44, cx + 50, cy + 72], 15, 165, fill=(245, 245, 240))
    d.line([(cx - 58, cy + 72), (cx + 58, cy + 72)], fill=(120, 50, 50), width=3)
    return im


def make_pattern(size=(640, 480)):
    """Colour bars, a checker board and a gradient: something with structure."""
    w, h = size
    im = Image.new("RGB", size, (20, 20, 20))
    d = ImageDraw.Draw(im)
    bars = [(255, 255, 255), (255, 230, 0), (0, 210, 220), (0, 200, 60), (220, 0, 200), (230, 30, 30), (30, 60, 220), (20, 20, 20)]
    bw = w / len(bars)
    for i, c in enumerate(bars):
        d.rectangle([round(i * bw), 0, round((i + 1) * bw) - 1, int(h * 0.42)], fill=c)
    # checker board
    cs = 32
    for y in range(int(h * 0.42), int(h * 0.72), cs):
        for x in range(0, w, cs):
            if ((x // cs) + (y // cs)) % 2 == 0:
                d.rectangle([x, y, x + cs - 1, min(y + cs - 1, int(h * 0.72) - 1)], fill=(235, 235, 235))
            else:
                d.rectangle([x, y, x + cs - 1, min(y + cs - 1, int(h * 0.72) - 1)], fill=(40, 90, 160))
    # gradient strip
    for x in range(w):
        t = x / (w - 1)
        d.line([(x, int(h * 0.72)), (x, int(h * 0.82))], fill=(round(255 * t), round(120 + 100 * math.sin(t * math.pi)), round(255 * (1 - t))))
    # label
    d.rectangle([0, int(h * 0.82), w, h], fill=(30, 30, 34))
    f = font(int(h * 0.09))
    text = "FakeCamera"
    tw = d.textlength(text, font=f)
    d.text(((w - tw) / 2, int(h * 0.835)), text, font=f, fill=(255, 255, 255))
    return im


def make_objects(size=(640, 480)):
    """Big coloured objects on a table, for 'show me something red' mini-games."""
    w, h = size
    im = vertical_gradient(size, (232, 228, 220), (150, 118, 92))
    d = ImageDraw.Draw(im)
    d.rectangle([0, int(h * 0.55), w, h], fill=(120, 86, 60))
    for x in range(0, w, 80):
        d.line([(x, int(h * 0.55)), (x - 60, h)], fill=(104, 72, 50), width=2)
    items = [((230, 30, 30), "ball"), ((30, 90, 230), "cube"), ((250, 220, 20), "banana"), ((40, 170, 60), "cylinder"), ((250, 130, 20), "ball"), ((150, 50, 200), "cube")]
    slot = w / len(items)
    for i, (color, shape) in enumerate(items):
        cx, cy = int(slot * (i + 0.5)), int(h * 0.66)
        r = int(slot * 0.42)
        shadow = (90, 64, 44)
        d.ellipse([cx - r, cy + r - 16, cx + r, cy + r + 10], fill=shadow)
        if shape == "ball":
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)
            d.ellipse([cx - r // 2, cy - r + 8, cx - r // 6, cy - r // 2], fill=tuple(min(255, c + 90) for c in color))
        elif shape == "cube":
            d.rectangle([cx - r, cy - r, cx + r, cy + r], fill=color)
            d.polygon([(cx - r, cy - r), (cx - r + 18, cy - r - 22), (cx + r + 18, cy - r - 22), (cx + r, cy - r)], fill=tuple(min(255, c + 60) for c in color))
            d.polygon([(cx + r, cy - r), (cx + r + 18, cy - r - 22), (cx + r + 18, cy + r - 22), (cx + r, cy + r)], fill=tuple(max(0, c - 50) for c in color))
        elif shape == "banana":
            d.chord([cx - r - 10, cy - r + 10, cx + r + 10, cy + r + 30], 200, 340, fill=color)
            d.chord([cx - r - 10, cy - r + 34, cx + r + 10, cy + r + 54], 200, 340, fill=(232, 228, 220))
            d.pieslice([cx - r - 10, cy - r + 10, cx + r + 10, cy + r + 30], 200, 340, fill=color)
        else:
            d.rectangle([cx - r + 6, cy - r, cx + r - 6, cy + r], fill=color)
            d.ellipse([cx - r + 6, cy + r - 14, cx + r - 6, cy + r + 14], fill=tuple(max(0, c - 40) for c in color))
            d.ellipse([cx - r + 6, cy - r - 14, cx + r - 6, cy - r + 14], fill=tuple(min(255, c + 50) for c in color))
    return im


def make_room(size=(960, 540)):
    """A wide living-room view: the part shown scrolls with the tilt."""
    w, h = size
    im = Image.new("RGB", size, (214, 200, 176))
    d = ImageDraw.Draw(im)
    floor_y = int(h * 0.68)
    d.rectangle([0, floor_y, w, h], fill=(150, 104, 66))
    for i in range(-2, 40):
        x = i * 52
        d.line([(x, floor_y), (x + 140, h)], fill=(128, 86, 52), width=2)
    d.rectangle([0, floor_y - 12, w, floor_y], fill=(238, 232, 220))  # skirting board
    # window with sky
    d.rectangle([int(w * 0.36), int(h * 0.12), int(w * 0.64), int(h * 0.5)], fill=(255, 255, 255))
    sky = vertical_gradient((int(w * 0.26), int(h * 0.34)), (120, 180, 240), (200, 230, 250))
    im.paste(sky, (int(w * 0.37), int(h * 0.14)))
    d.rectangle([int(w * 0.37), int(h * 0.42), int(w * 0.63), int(h * 0.48)], fill=(90, 150, 70))
    d.ellipse([int(w * 0.55), int(h * 0.16), int(w * 0.6), int(h * 0.25)], fill=(255, 240, 150))
    d.line([(w // 2, int(h * 0.14)), (w // 2, int(h * 0.48))], fill=(255, 255, 255), width=8)
    d.line([(int(w * 0.37), int(h * 0.31)), (int(w * 0.63), int(h * 0.31))], fill=(255, 255, 255), width=8)
    # door
    d.rectangle([int(w * 0.06), int(h * 0.2), int(w * 0.18), floor_y], fill=(110, 70, 40), outline=(80, 50, 30), width=4)
    d.ellipse([int(w * 0.155), int(h * 0.44), int(w * 0.17), int(h * 0.47)], fill=(230, 200, 90))
    # bookshelf
    sx0, sx1 = int(w * 0.72), int(w * 0.9)
    d.rectangle([sx0, int(h * 0.22), sx1, floor_y], fill=(96, 60, 36))
    random.seed(4)
    for row in range(3):
        y1 = int(h * 0.25) + row * int(h * 0.14)
        y2 = y1 + int(h * 0.12)
        x = sx0 + 8
        while x < sx1 - 14:
            bw = random.randint(10, 22)
            d.rectangle([x, y1 + random.randint(0, 10), x + bw, y2], fill=random.choice([(200, 60, 60), (60, 90, 200), (60, 160, 90), (230, 190, 60), (240, 240, 230), (140, 60, 160)]))
            x += bw + 3
        d.rectangle([sx0, y2, sx1, y2 + 6], fill=(70, 44, 26))
    # picture frame and lamp
    d.rectangle([int(w * 0.22), int(h * 0.2), int(w * 0.32), int(h * 0.38)], fill=(40, 30, 20))
    d.rectangle([int(w * 0.23), int(h * 0.215), int(w * 0.31), int(h * 0.365)], fill=(230, 120, 80))
    d.ellipse([int(w * 0.245), int(h * 0.24), int(w * 0.295), int(h * 0.34)], fill=(250, 200, 90))
    d.rectangle([int(w * 0.66), int(h * 0.5), int(w * 0.67), floor_y], fill=(60, 60, 60))
    d.polygon([(int(w * 0.62), int(h * 0.5)), (int(w * 0.71), int(h * 0.5)), (int(w * 0.695), int(h * 0.4)), (int(w * 0.635), int(h * 0.4))], fill=(250, 235, 180))
    # sofa
    d.rounded_rectangle([int(w * 0.2), int(h * 0.56), int(w * 0.6), floor_y + 30], radius=18, fill=(70, 100, 150))
    d.rounded_rectangle([int(w * 0.22), int(h * 0.62), int(w * 0.39), floor_y + 10], radius=12, fill=(90, 120, 170))
    d.rounded_rectangle([int(w * 0.41), int(h * 0.62), int(w * 0.58), floor_y + 10], radius=12, fill=(90, 120, 170))
    return im


def make_arena(size=(640, 480)):
    """Outdoors: sky, hills and a lawn, as a backdrop for AR fights or players."""
    w, h = size
    im = vertical_gradient(size, (80, 140, 230), (190, 225, 250))
    d = ImageDraw.Draw(im)
    d.ellipse([int(w * 0.78), int(h * 0.08), int(w * 0.9), int(h * 0.27)], fill=(255, 245, 190))
    for i, (cx, r, c) in enumerate([(int(w * 0.15), 260, (90, 140, 90)), (int(w * 0.55), 320, (70, 120, 80)), (int(w * 0.95), 280, (100, 150, 95))]):
        d.ellipse([cx - r, int(h * 0.5) - r // 3, cx + r, int(h * 0.5) + r // 3], fill=c)
    ground = vertical_gradient((w, int(h * 0.5)), (110, 170, 70), (60, 120, 45))
    im.paste(ground, (0, int(h * 0.5)))
    d = ImageDraw.Draw(im)
    for i in range(6):
        y = int(h * 0.55) + i * int(h * 0.075)
        d.line([(0, y), (w, y + 6)], fill=(90, 150, 60), width=2)
    for x in range(0, w, 90):
        d.rectangle([x + 20, int(h * 0.45), x + 26, int(h * 0.52)], fill=(90, 60, 40))
        d.ellipse([x - 4, int(h * 0.36), x + 50, int(h * 0.47)], fill=(50, 110, 60))
    return im


def draw_card(d, number, x, y, card_w, cell):
    """One AR Play card: white card, black 5x5 marker, grey number."""
    card_h = int(card_w * 1.4)
    d.rounded_rectangle([x, y, x + card_w, y + card_h], radius=card_w // 10, fill=(255, 255, 255), outline=(60, 60, 60), width=2)
    m = 5 * cell
    mx = x + (card_w - m) // 2
    my = y + int(card_w * 0.12)
    for r, row in enumerate(AR_CARDS[number]):
        for c, ch in enumerate(row):
            if ch == "#":
                d.rectangle([mx + c * cell, my + r * cell, mx + (c + 1) * cell - 1, my + (r + 1) * cell - 1], fill=(0, 0, 0))
    f = font(int(card_w * 0.22))
    text = f"{number:02d}"
    tw = d.textlength(text, font=f)
    d.text((x + (card_w - tw) / 2, my + m + int(card_w * 0.06)), text, font=f, fill=(140, 140, 140))


def table_background(size):
    w, h = size
    im = vertical_gradient(size, (222, 196, 160), (196, 160, 118))
    d = ImageDraw.Draw(im)
    for y in range(0, h, 48):
        d.line([(0, y), (w, y)], fill=(184, 150, 108), width=2)
    return im


def make_ar_table(size=(640, 480)):
    """The six AR Play cards laid out: 1-2-3 in a row, 4 and 5 on the sides, 6 at the top."""
    w, h = size
    im = table_background(size)
    d = ImageDraw.Draw(im)
    card_w, cell = 96, 13
    card_h = int(card_w * 1.4)
    y_mid = int(h * 0.68) - card_h // 2
    gap = 14
    total = 5 * card_w + 4 * gap
    x0 = (w - total) // 2
    for i, number in enumerate([4, 1, 2, 3, 5]):
        draw_card(d, number, x0 + i * (card_w + gap), y_mid, card_w, cell)
    draw_card(d, 6, (w - card_w) // 2, int(h * 0.05), card_w, cell)
    return im


def make_ar_card(size=(640, 480), number=1):
    """One big AR Play card, for the games which need a single card."""
    w, h = size
    im = table_background(size)
    d = ImageDraw.Draw(im)
    card_w, cell = 260, 34
    draw_card(d, number, (w - card_w) // 2, (h - int(card_w * 1.4)) // 2, card_w, cell)
    return im


def make_qr(size=(320, 240), text="https://github.com/delon5/FakeCamera"):
    import qrcode
    qr = qrcode.QRCode(border=2, box_size=6)
    qr.add_data(text)
    qr.make(fit=True)
    code = qr.make_image(fill_color="black", back_color="white").convert("RGB")
    scale = min(size[0] / code.width, size[1] / code.height)
    code = code.resize((int(code.width * scale), int(code.height * scale)), Image.NEAREST)
    im = Image.new("RGB", size, (255, 255, 255))
    im.paste(code, ((size[0] - code.width) // 2, (size[1] - code.height) // 2))
    return im


def make_avatar(size=(64, 64)):
    """A tiny camera icon, for titles which only accept small pictures."""
    w, h = size
    im = vertical_gradient(size, (40, 60, 110), (20, 30, 60))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([8, 20, 56, 52], radius=6, fill=(230, 230, 235))
    d.rectangle([20, 13, 36, 21], fill=(230, 230, 235))
    d.ellipse([22, 26, 42, 46], fill=(40, 40, 50))
    d.ellipse([27, 31, 37, 41], fill=(90, 150, 220))
    d.ellipse([29, 33, 32, 36], fill=(255, 255, 255))
    d.rectangle([46, 26, 51, 30], fill=(230, 60, 60))
    return im


def make_white(size=(320, 240)):
    return Image.new("RGB", size, (255, 255, 255))


def main():
    os.makedirs(OUT, exist_ok=True)
    save(make_face(), "face.bmp")
    save(make_pattern(), "pattern.bmp")
    save(make_objects(), "objects.bmp")
    save(make_room(), "room.bmp")
    save(make_arena(), "arena.bmp")
    save(make_ar_table(), "ar-table.bmp")
    save(make_ar_card(), "ar-card.bmp")
    save(make_qr(), "qr.bmp")
    save(make_avatar(), "avatar.bmp")
    save(make_white(), "white.bmp")


if __name__ == "__main__":
    main()
