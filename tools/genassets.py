#!/usr/bin/env python3
"""Generate the HOME Menu icon, banner image and banner jingle in meta/.

Usage: python3 tools/genassets.py path/to/Inter-ExtraBold.otf
"""
import math
import random
import struct
import sys
import wave
from PIL import Image, ImageDraw, ImageFont

COLORS = [(242, 194, 48), (232, 74, 95), (61, 190, 139), (76, 141, 240), (180, 95, 224), (240, 138, 60)]
SKIN, SHIRT, PANTS, SHOE, HAIR = (237, 191, 150), (46, 196, 182), (59, 63, 88), (217, 72, 59), (58, 42, 30)


def wall(w, h, seed):
    """Grey sanded climbing wall with panel seams and bolt holes."""
    rnd = random.Random(seed)
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        base = [int(140 - 50 * t)] * 3
        base[2] += 10
        for x in range(w):
            n = rnd.randint(-7, 7)
            px[x, y] = tuple(max(0, min(255, c + n)) for c in base)
    d = ImageDraw.Draw(img)
    step = max(w // 4, 12)
    for x in range(0, w, step):
        d.line([(x, 0), (x, h)], fill=(70, 72, 80))
    for y in range(0, h, step):
        d.line([(0, y), (w, y)], fill=(70, 72, 80))
    return img


def hold(d, x, y, r, col):
    d.ellipse([x - r + 1, y - r + 2, x + r + 1, y + r + 2], fill=(30, 30, 36))
    d.ellipse([x - r, y - r, x + r, y + r], fill=col)
    light = tuple(min(255, c + 70) for c in col)
    q = max(1, r // 3)
    d.ellipse([x - r // 2 - q, y - r // 2 - q, x - r // 2 + q, y - r // 2 + q], fill=light)


def capsule(d, a, b, r, col):
    d.line([a, b], fill=col, width=int(r * 2))
    for (x, y) in (a, b):
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)


def climber(d, x, y, s):
    """A climber seen from behind, reaching up with the right hand. (x, y) is the torso."""
    sh_l, sh_r = (x - 3 * s, y - 6 * s), (x + 3 * s, y - 6 * s)
    hip_l, hip_r = (x - 2 * s, y + 5 * s), (x + 2 * s, y + 5 * s)
    capsule(d, hip_l, (x - 7 * s, y + 9 * s), 1.4 * s, PANTS)
    capsule(d, (x - 7 * s, y + 9 * s), (x - 5 * s, y + 15 * s), 1.2 * s, PANTS)
    capsule(d, hip_r, (x + 6 * s, y + 8 * s), 1.4 * s, PANTS)
    capsule(d, (x + 6 * s, y + 8 * s), (x + 6 * s, y + 13 * s), 1.2 * s, PANTS)
    capsule(d, (x - 5 * s, y + 15 * s), (x - 6.5 * s, y + 15.5 * s), 1.1 * s, SHOE)
    capsule(d, (x + 6 * s, y + 13 * s), (x + 7.5 * s, y + 13.5 * s), 1.1 * s, SHOE)
    capsule(d, (x, y - 5 * s), (x, y + 3 * s), 3.2 * s, SHIRT)
    capsule(d, hip_l, hip_r, 1.8 * s, PANTS)
    capsule(d, sh_l, (x - 7 * s, y - 9 * s), 1.2 * s, SHIRT)
    capsule(d, (x - 7 * s, y - 9 * s), (x - 6 * s, y - 15 * s), 1.0 * s, SKIN)
    capsule(d, sh_r, (x + 5 * s, y - 13 * s), 1.2 * s, SHIRT)
    capsule(d, (x + 5 * s, y - 13 * s), (x + 6 * s, y - 20 * s), 1.0 * s, SKIN)
    r = 2.4 * s
    d.ellipse([x - r, y - 10 * s - r, x + r, y - 10 * s + r], fill=HAIR)


def icon():
    img = wall(48, 48, 1)
    d = ImageDraw.Draw(img)
    for (x, y, r, c) in [(10, 8, 3, 0), (34, 6, 4, 1), (16, 22, 2, 2), (38, 26, 3, 3), (8, 36, 3, 4), (30, 42, 2, 5)]:
        hold(d, x, y, r, COLORS[c])
    climber(d, 24, 28, 1.25)
    img.save("meta/icon.png")


def banner(font_path):
    w, h = 256, 128
    img = wall(w, h, 2)
    d = ImageDraw.Draw(img)
    rnd = random.Random(5)
    for _ in range(26):
        hold(d, rnd.randint(6, w - 6), rnd.randint(6, h - 6), rnd.randint(2, 6), COLORS[rnd.randrange(len(COLORS))])
    climber(d, 214, 80, 2.2)
    img = img.convert("RGBA")
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shade).rectangle([0, 28, 180, 98], fill=(10, 12, 18, 175))
    img = Image.alpha_composite(img, shade)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(font_path, 34)
    small = ImageFont.truetype(font_path, 11)
    d.text((14, 36 + 2), "BOULDER", font=font, fill=(0, 0, 0, 160))
    d.text((12, 36), "BOULDER", font=font, fill=(242, 242, 245))
    d.text((14, 78), "SET  -  CLIMB  -  SEND", font=small, fill=(61, 214, 196))
    img.convert("RGB").save("meta/banner.png")


def jingle():
    rate = 22050
    notes = [(440.0, 0.11), (523.25, 0.11), (659.25, 0.11), (783.99, 0.11), (880.0, 0.5)]  # A C E G A
    samples = []
    for freq, dur in notes:
        n = int(rate * dur)
        for i in range(n):
            t = i / rate
            env = math.exp(-t * 5) * min(1.0, i / 150)
            v = (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t)) * env
            samples.append(int(max(-1, min(1, v * 0.5)) * 32767))
    samples += [0] * int(rate * 0.6)
    with wave.open("meta/banner.wav", "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(struct.pack("<%dh" % len(samples), *samples))


if __name__ == "__main__":
    icon()
    banner(sys.argv[1])
    jingle()
