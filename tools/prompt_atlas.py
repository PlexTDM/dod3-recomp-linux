#!/usr/bin/env python3
"""Draw the button-prompt atlases the port shows in place of the game's.

Every button glyph the game draws is a sprite of one layout
(HUD_COMMON.menu.icon_button) over one 256x128 atlas
(HUD_COMMON.Texture.icon_button, PlayStation 3 buttons). This draws the same
28 rectangles for other pads, at four times the resolution, as the PNGs in
src/res/prompts that the build embeds and src/patches/prompts.cpp serves.
Nothing here comes from the game's art: the rectangles are the layout's
numbers, the glyphs are drawn from scratch.

usage: python tools/prompt_atlas.py --font <bold sans .ttf> [--out DIR] [--scale 4] [SET ...]

The committed PNGs were drawn with Roboto Bold (Apache License 2.0). Needs
Pillow (tools/requirements.txt).
"""
import argparse
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 256, 128   # the game's atlas, in its own texels

# sprite number -> (cell x, y, w, h), the glyph's box inside the cell (x0, y0, x1, y1)
CELLS = {
    0: ((32, 31, 31, 32), (3, 4, 28, 29)),    1: ((0, 31, 31, 32), (3, 4, 28, 29)),
    2: ((32, 0, 31, 32), (3, 4, 28, 29)),     3: ((0, 0, 31, 32), (3, 4, 28, 29)),
    4: ((0, 63, 45, 25), (5, 3, 40, 22)),     5: ((0, 89, 45, 38), (5, 9, 40, 28)),
    6: ((63, 1, 45, 25), (5, 3, 40, 22)),     7: ((63, 27, 45, 38), (5, 9, 40, 28)),
    8: ((46, 66, 31, 28), (1, 1, 29, 27)),    9: ((79, 66, 36, 28), (1, 2, 35, 27)),
    10: ((50, 97, 26, 26), (2, 2, 24, 24)),   11: ((76, 97, 26, 26), (2, 2, 24, 24)),
    13: ((109, 0, 34, 33), (5, 5, 28, 28)),   14: ((143, 0, 33, 33), (5, 5, 28, 28)),
    15: ((177, 0, 33, 33), (5, 5, 28, 28)),
    16: ((147, 64, 30, 29), (1, 1, 28, 28)),  17: ((115, 64, 30, 29), (1, 1, 28, 28)),
    18: ((147, 33, 30, 29), (1, 1, 28, 28)),  19: ((115, 33, 30, 29), (1, 1, 28, 28)),
    20: ((102, 97, 26, 26), (2, 2, 24, 24)),  21: ((128, 97, 26, 26), (2, 2, 24, 24)),
    22: ((181, 35, 26, 26), (3, 4, 22, 22)),  23: ((180, 61, 18, 16), (3, 2, 15, 14)),
    24: ((224, 94, 31, 32), (1, 2, 30, 31)),  25: ((192, 94, 31, 32), (1, 2, 30, 31)),
    26: ((159, 94, 31, 31), (1, 2, 30, 31)),  27: ((224, 63, 31, 32), (1, 2, 30, 31)),
    28: ((180, 78, 18, 16), (3, 2, 15, 14)),
}
# a face button's three sprites: in text, pressed (pale, on a faint disc), large
FACE = {"circle": (0, 16, 24), "cross": (1, 17, 25), "triangle": (2, 18, 26), "square": (3, 19, 27)}

WHITE = (240, 240, 240)
PS_COLOURS = {"cross": (124, 160, 228), "circle": (240, 96, 88), "square": (232, 124, 190), "triangle": (64, 208, 190)}
PS_PLAIN = {k: (226, 230, 238) for k in PS_COLOURS}
ARROWS = {"u": "arrow", "d": "arrow", "l": "arrow", "r": "arrow"}

# face: PlayStation position -> glyph ("shape" draws the position's own symbol) and colour
# start, select: (kind, label); sticks, clicks: the disc's letters
# keys: a keyboard's own drawing of the D-pad and the sticks (direction -> key cap text)
SETS = {
    "ps4": dict(face={k: ("shape", c) for k, c in PS_COLOURS.items()},
                l1="L1", l2="L2", r1="R1", r2="R2", start=("oval", "OPTIONS"), select=("oval", "SHARE"),
                ls="L", rs="R", l3="L3", r3="R3"),
    "ps5": dict(face={k: ("shape", c) for k, c in PS_PLAIN.items()},
                l1="L1", l2="L2", r1="R1", r2="R2", start=("menu", None), select=("create", None),
                ls="L", rs="R", l3="L3", r3="R3"),
    "xbox": dict(face={"cross": ("A", (108, 196, 60)), "circle": ("B", (232, 64, 52)),
                       "square": ("X", (56, 132, 232)), "triangle": ("Y", (240, 188, 36))},
                 l1="LB", l2="LT", r1="RB", r2="RT", start=("menu", None), select=("view", None),
                 ls="L", rs="R", l3="LS", r3="RS"),
    "switch": dict(face={"cross": ("B", WHITE), "circle": ("A", WHITE), "square": ("Y", WHITE), "triangle": ("X", WHITE)},
                   l1="L", l2="ZL", r1="R", r2="ZR", start=("plus", None), select=("minus", None),
                   ls="L", rs="R", l3="LS", r3="RS"),
    # The keyboard that stands in for a pad (ps3recomp/libs/input/cellPad.c):
    # Z X A S, Q W, 1 2, Enter, Tab. Windows: the arrows are the D-pad and the
    # left stick. macOS: the arrows are the left stick, T F G H the D-pad,
    # I J K L the right stick.
    "keyboard": dict(face={"cross": ("Z", WHITE), "circle": ("X", WHITE), "square": ("A", WHITE), "triangle": ("S", WHITE)},
                     l1="Q", l2="1", r1="W", r2="2", start=("key", "Enter"), select=("key", "Tab"),
                     ls="L", rs="R", l3="L3", r3="R3", keys=dict(dpad=ARROWS, ls=ARROWS)),
    "keyboard_mac": dict(face={"cross": ("Z", WHITE), "circle": ("X", WHITE), "square": ("A", WHITE), "triangle": ("S", WHITE)},
                         l1="Q", l2="1", r1="W", r2="2", start=("key", "Return"), select=("key", "Tab"),
                         ls="L", rs="R", l3="L3", r3="R3",
                         keys=dict(dpad={"u": "T", "d": "G", "l": "F", "r": "H"}, ls=ARROWS,
                                   rs={"u": "I", "d": "K", "l": "J", "r": "L"})),
}

RIM = (150, 154, 160, 255)
LIT = (92, 222, 200, 255)


class Pen:
    """Drawing in the atlas's texels, on a canvas `scale * 4` times larger."""

    def __init__(self, scale, font):
        self.scale, self.z = scale, scale * 4
        self.im = Image.new("RGBA", (W * self.z, H * self.z), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)
        self.font = font

    def box(self, idx):
        (cx, cy, _, _), (x0, y0, x1, y1) = CELLS[idx]
        return cx + x0, cy + y0, cx + x1, cy + y1

    def P(self, *v):
        return [c * self.z for c in v]

    def disc(self, cx, cy, r, fill):
        self.d.ellipse(self.P(cx - r, cy - r, cx + r, cy + r), fill=fill)

    def ring(self, cx, cy, r, col, w):
        self.d.ellipse(self.P(cx - r, cy - r, cx + r, cy + r), outline=col, width=max(1, int(w * self.z)))

    def body(self, cx, cy, r, top=(86, 90, 96), bot=(22, 23, 26), rim=RIM):
        """A round button: a rim, and a top-lit face."""
        if rim:
            self.disc(cx, cy, r, rim)
            r -= 0.9
        z = self.z
        n = int(2 * r * z) + 2
        g = Image.new("RGBA", (n, n))
        for y in range(n):
            t = y / (n - 1)
            g.paste(tuple(int(a + (b - a) * t) for a, b in zip(top, bot)) + (255,), (0, y, n, y + 1))
        m = Image.new("L", (n, n), 0)
        ImageDraw.Draw(m).ellipse((0, 0, n - 1, n - 1), fill=255)
        self.im.paste(g, (int((cx - r) * z), int((cy - r) * z)), m)

    def rrect(self, x0, y0, x1, y1, rad, fill, rim=None, rw=0.9):
        if rim:
            self.d.rounded_rectangle(self.P(x0, y0, x1, y1), radius=rad * self.z, fill=rim)
            x0 += rw; y0 += rw; x1 -= rw; y1 -= rw
            rad = max(0.2, rad - rw)
        self.d.rounded_rectangle(self.P(x0, y0, x1, y1), radius=rad * self.z, fill=fill)

    def text(self, cx, cy, s, size, fill, fit=None):
        f = ImageFont.truetype(self.font, int(size * self.z))
        l, t, r, b = self.d.textbbox((0, 0), s, font=f)
        if fit and (r - l) > fit * self.z:
            f = ImageFont.truetype(self.font, int(size * self.z * fit * self.z / (r - l)))
            l, t, r, b = self.d.textbbox((0, 0), s, font=f)
        self.d.text((cx * self.z - (l + r) / 2, cy * self.z - (t + b) / 2), s, font=f, fill=fill)

    def line(self, pts, fill, w):
        self.d.line([c * self.z for p in pts for c in p], fill=fill, width=max(1, int(w * self.z)), joint="curve")
        for x, y in pts:
            self.disc(x, y, w / 2, fill)

    def shape(self, name, cx, cy, r, col, w):
        """A PlayStation face button's symbol."""
        if name == "circle":
            self.ring(cx, cy, r, col, w)
        elif name == "cross":
            q = r * 0.8
            self.line([(cx - q, cy - q), (cx + q, cy + q)], col, w)
            self.line([(cx - q, cy + q), (cx + q, cy - q)], col, w)
        elif name == "square":
            q = r * 0.82
            self.line([(cx - q, cy - q), (cx + q, cy - q), (cx + q, cy + q), (cx - q, cy + q), (cx - q, cy - q)], col, w)
        elif name == "triangle":
            q = r * 1.02
            a, b, c = (cx, cy - q), (cx + q * 0.92, cy + q * 0.62), (cx - q * 0.92, cy + q * 0.62)
            self.line([a, b, c, a], col, w)

    def keycap(self, x0, y0, x1, y1, label, size, col=WHITE + (255,), lit=False):
        self.rrect(x0, y0, x1, y1, min(3.0, (x1 - x0) / 4), (44, 46, 52, 255), rim=(176, 180, 188, 255), rw=min(1.0, (x1 - x0) / 9))
        e = min(1.6, (x1 - x0) / 6)
        self.rrect(x0 + e, y0 + e * 0.8, x1 - e, y1 - e * 1.6, min(2.0, (x1 - x0) / 6), (70, 73, 80, 255))
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 - e * 0.5
        if label == "arrow":
            return cx, cy
        self.text(cx, cy, label, size, LIT if lit else col, fit=(x1 - x0) - 2 * e - 1)
        return None

    def arrow(self, cx, cy, dx, dy, q, col):
        self.d.polygon(self.P(cx + dx * q, cy + dy * q, cx - dx * q + dy * q, cy - dy * q + dx * q,
                              cx - dx * q - dy * q, cy - dy * q - dx * q), fill=col)

    def cluster(self, x0, y0, x1, y1, keys, lit=()):
        """Four key caps in the D-pad's plus: a keyboard's directions."""
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        k = (x1 - x0) / 3 - 0.2
        for n, (dx, dy) in {"u": (0, -1), "d": (0, 1), "l": (-1, 0), "r": (1, 0)}.items():
            kx, ky = cx + dx * (k + 0.3), cy + dy * (k + 0.3)
            c = self.keycap(kx - k / 2, ky - k / 2, kx + k / 2, ky + k / 2, keys[n], k * 0.86, lit=n in lit)
            if c:
                self.arrow(c[0], c[1], dx, dy, k * 0.2, LIT if n in lit else WHITE + (255,))


def draw(name, scale, font):
    S = SETS[name]
    p = Pen(scale, font)
    keyboard = "keys" in S

    for pos, (i_small, i_pressed, i_large) in FACE.items():
        label, col = S["face"][pos]
        for idx, mode in ((i_small, "small"), (i_pressed, "pressed"), (i_large, "large")):
            x0, y0, x1, y1 = p.box(idx)
            cx, cy, r = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2
            c = col + (255,)
            if mode == "pressed":
                c = tuple(int(v + (255 - v) * 0.72) for v in col) + (255,)
            if keyboard:
                if mode == "pressed":
                    p.rrect(x0 + 2, y0 + 2, x1 - 2, y1 - 2, 3.0, (120, 124, 132, 110))
                    p.text(cx, cy - 0.5, label, 15, c)
                else:
                    p.keycap(x0 + 1.5, y0 + 1.5, x1 - 1.5, y1 - 1.5, label, 14 if mode == "small" else 17)
                continue
            if mode == "pressed":
                p.disc(cx, cy, r, (120, 124, 132, 70))
            else:
                p.body(cx, cy, r)
            if label == "shape":
                p.shape(pos, cx, cy, r * 0.52, c, r * 0.15)
            else:
                p.text(cx, cy, label, r * 1.35, c)

    for idx, label in ((4, S["l1"]), (5, S["l2"]), (6, S["r1"]), (7, S["r2"])):
        x0, y0, x1, y1 = p.box(idx)
        if keyboard:
            cx = (x0 + x1) / 2
            p.keycap(cx - 10.5, y0 - 0.5, cx + 10.5, y1 + 0.5, label, 13)
        else:
            p.rrect(x0, y0, x1, y1, 4.5, (58, 61, 66, 255), rim=RIM)
            p.rrect(x0 + 1.8, y0 + 1.6, x1 - 1.8, y1 - 3.0, 3.2, (74, 78, 84, 255))
            p.text((x0 + x1) / 2, (y0 + y1) / 2 - 0.8, label, 13, WHITE + (255,))

    for idx, (kind, label) in ((8, S["start"]), (9, S["select"])):
        x0, y0, x1, y1 = p.box(idx)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        c = WHITE + (255,)
        if kind == "key":
            p.keycap(x0, cy - 9.5, x1, cy + 9.5, label, 10.5)
        elif kind == "oval":      # a small button with its name under it, as the game draws START
            p.rrect(cx - 4.2, y0 + 1, cx + 4.2, y0 + 14, 4.0, (58, 61, 66, 255), rim=RIM)
            p.rrect(x0, y1 - 10.5, x1, y1, 1.0, (14, 14, 16, 255))
            p.text(cx, y1 - 5.5, label, 8.6, c, fit=(x1 - x0) - 2.4)
        else:
            r = 10.5
            p.body(cx, cy, r)
            if kind == "menu":
                for dy in (-3.6, 0, 3.6):
                    p.line([(cx - 4.6, cy + dy), (cx + 4.6, cy + dy)], c, 1.5)
            elif kind == "view":
                p.d.rectangle(p.P(cx - 5.2, cy - 4.8, cx + 2.2, cy + 1.2), outline=c, width=int(1.3 * p.z))
                p.d.rectangle(p.P(cx - 2.2, cy - 1.6, cx + 5.2, cy + 4.6), fill=(40, 42, 46, 255), outline=c, width=int(1.3 * p.z))
            elif kind == "create":
                p.line([(cx, cy + 5.2), (cx, cy + 0.6)], c, 1.6)
                for ang in (-52, 0, 52):
                    a = math.radians(ang - 90)
                    p.line([(cx + 3.0 * math.cos(a), cy - 0.6 + 3.0 * math.sin(a)),
                            (cx + 6.0 * math.cos(a), cy - 0.6 + 6.0 * math.sin(a))], c, 1.4)
            elif kind == "plus":
                p.line([(cx - 5, cy), (cx + 5, cy)], c, 2.0)
                p.line([(cx, cy - 5), (cx, cy + 5)], c, 2.0)
            elif kind == "minus":
                p.line([(cx - 5, cy), (cx + 5, cy)], c, 2.0)

    for idx, label, which in ((10, S["ls"], "ls"), (11, S["rs"], "rs"), (20, S["l3"], None), (21, S["r3"], None)):
        x0, y0, x1, y1 = p.box(idx)
        cx, cy, r = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2
        if keyboard and which in S["keys"]:
            p.cluster(x0 - 0.5, y0 - 0.5, x1 + 0.5, y1 + 0.5, S["keys"][which])
            continue
        p.body(cx, cy, r, top=(96, 100, 106), bot=(18, 19, 22), rim=(10, 10, 12, 255))
        p.text(cx, cy - 0.3, label, 13 if len(label) == 1 else 10.5, WHITE + (255,), fit=2 * r - 4)

    # the D-pad: at rest, up and down lit, left and right lit
    for idx, lit in ((13, ()), (14, ("u", "d")), (15, ("l", "r"))):
        x0, y0, x1, y1 = p.box(idx)
        if keyboard:
            p.cluster(x0 - 0.5, y0 - 0.5, x1 + 0.5, y1 + 0.5, S["keys"]["dpad"], lit)
            continue
        cx, cy, a = (x0 + x1) / 2, (y0 + y1) / 2, 4.0
        edge, arm, hi = (12, 12, 14, 255), (58, 61, 66, 255), (104, 108, 114, 255)
        for grow, col in ((0.0, RIM), (-0.9, edge)):     # the rim the round buttons have, then the pad
            p.rrect(cx - a - 1.6 - grow, y0 - grow, cx + a + 1.6 + grow, y1 + grow, 2.2, col)
            p.rrect(x0 - grow, cy - a - 1.6 - grow, x1 + grow, cy + a + 1.6 + grow, 2.2, col)
        arms = {"u": (cx - a, y0 + 1.7, cx + a, cy - a + 0.4), "d": (cx - a, cy + a - 0.4, cx + a, y1 - 1.7),
                "l": (x0 + 1.7, cy - a, cx - a + 0.4, cy + a), "r": (cx + a - 0.4, cy - a, x1 - 1.7, cy + a)}
        for n, b in arms.items():
            p.rrect(*b, 1.0, LIT if n in lit else arm)
            if n not in lit:      # a lighter top edge, as a raised arm has
                p.rrect(b[0] + 0.5, b[1] + 0.4, b[2] - 0.5, b[1] + 1.4, 0.5, hi)
        p.rrect(cx - a, cy - a, cx + a, cy + a, 0.6, (40, 42, 46, 255))
        p.disc(cx, cy, 1.5, (20, 21, 24, 255))

    # the three symbols the text uses beside the buttons
    grey = (124, 126, 124, 255)
    x0, y0, x1, y1 = p.box(22)
    cx, cy, R = (x0 + x1) / 2, (y0 + y1) / 2 + 0.6, (x1 - x0) / 2
    star = []
    for i in range(11):
        ang = -math.pi / 2 + i * math.pi / 5
        rr = R if i % 2 == 0 else R * 0.42
        star.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    p.line(star, grey, 1.1)
    for idx, ch in ((23, "R"), (28, "C")):
        x0, y0, x1, y1 = p.box(idx)
        cx, cy, r = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2
        p.ring(cx, cy, r, grey, 1.0)
        p.text(cx, cy, ch, 8.5, grey)

    return finish(p.im, (W * scale, H * scale))


def finish(im, size):
    """Down to the atlas's size with the alpha weighted in, then the colour of
    each glyph's edge spread into the transparent texels beside it, so that a
    bilinear tap across the edge does not darken it."""
    im = im.convert("RGBa").resize(size, Image.LANCZOS).convert("RGBA")
    a = im.getchannel("A")
    spread = im.convert("RGBa").filter(ImageFilter.GaussianBlur(3)).convert("RGBA")
    spread.putalpha(255)
    rgb = Image.composite(im.convert("RGB"), spread.convert("RGB"), a.point(lambda v: 255 if v else 0))
    rgb.putalpha(a)
    return rgb


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("sets", nargs="*", metavar="SET", help="default: all of " + " ".join(sorted(SETS)))
    ap.add_argument("--font", required=True, help="a bold sans-serif TrueType font")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "res", "prompts"))
    ap.add_argument("--scale", type=int, default=4, help="times the game's 256x128 (default 4)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name in a.sets or sorted(SETS):
        if name not in SETS:
            ap.error("no set named " + name)
        path = os.path.join(a.out, name + ".png")
        draw(name, a.scale, a.font).save(path, optimize=True)
        print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
