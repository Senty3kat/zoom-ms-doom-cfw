"""Doom CFW boot logo: the DOOM lettering redrawn for the 128x64 LCD (rows 1.4x tall).

    python -I make_doom_logo.py OUT.bin OUT.png

Drawn as polygons in a square-pixel field of 128 x 90 at 4x, after the shape of the
game's M_DOOM: blocky letters with slot counters, a straight top and a bowed bottom
(outer letters reach lower), and the extruded underside as a 50 % dither band.
Then reduced to 128x64 (each LCD row = 1.4 field rows) and packed like BIN/133
(8 pages x 128 columns, bit 0 on top). PIL + numpy.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

S = 4                     # supersampling
FW, FH = 128, 90          # field in square pixels (64 LCD rows x 1.4)
TOP = 16
GAP = 3


def bottom(x):
    """letter bottom edge: a bowl, lowest at the outer edges"""
    u = (x - 64) / 60.0
    return 58 + 13 * u * u


def letters():
    """[(kind, x0, x1)] for D O O M, centred"""
    widths = [("D", 27), ("O", 27), ("O", 27), ("M", 33)]
    total = sum(w for _, w in widths) + GAP * 3
    x = (FW - total) / 2
    out = []
    for k, w in widths:
        out.append((k, x, x + w))
        x += w + GAP
    return out


def poly(draw, pts, fill):
    draw.polygon([(px * S, py * S) for px, py in pts], fill=fill)


def outline_pts(kind, x0, x1):
    """outer contour of a letter (clockwise), bottom following the bowl"""
    c = 5                                           # chamfer
    yb0, yb1 = bottom(x0), bottom(x1)
    if kind == "D":
        return [(x0, TOP), (x1 - c, TOP), (x1, TOP + c), (x1, yb1 - c), (x1 - c, yb1), (x0, yb0)]
    if kind == "O":
        return [(x0 + c, TOP), (x1 - c, TOP), (x1, TOP + c), (x1, yb1 - c), (x1 - c, yb1),
                (x0 + c, yb0), (x0, yb0 - c), (x0, TOP + c)]
    # M: legs plus the middle V; notch at the bottom middle
    m = (x0 + x1) / 2
    return [(x0, TOP), (x0 + 9, TOP), (m, TOP + 13), (x1 - 9, TOP), (x1, TOP), (x1, yb1),
            (x1 - 9, bottom(x1 - 9)), (x1 - 9, TOP + 18), (m, TOP + 30), (x0 + 9, TOP + 18),
            (x0 + 9, bottom(x0 + 9)), (x0, yb0)]


def counter_pts(kind, x0, x1):
    """the slot inside D and O"""
    if kind == "M":
        return None
    a, b = x0 + 10, x1 - 10
    return [(a, TOP + 9), (b, TOP + 9), (b, bottom(b) - 11), (a, bottom(a) - 11)]


def render():
    face = Image.new("L", (FW * S, FH * S), 0)
    under = Image.new("L", (FW * S, FH * S), 0)
    solid = Image.new("L", (FW * S, FH * S), 0)          # letters with their counters filled
    df, du, ds = ImageDraw.Draw(face), ImageDraw.Draw(under), ImageDraw.Draw(solid)
    for kind, x0, x1 in letters():
        o = outline_pts(kind, x0, x1)
        poly(du, [(px, py + 9 + 3 * abs(px - 64) / 64) for px, py in o], 255)   # extrusion
        poly(df, o, 255)
        poly(ds, o, 255)
        cp = counter_pts(kind, x0, x1)
        if cp:
            poly(df, cp, 0)
    f = np.asarray(face.resize((FW, FH), Image.BOX)) > 127
    u = np.asarray(under.resize((FW, FH), Image.BOX)) > 127
    s = np.asarray(solid.resize((FW, FH), Image.BOX)) > 40     # the extrusion never shows through
    return f, u & ~s


def to_lcd(face, under):
    lcd = np.zeros((64, 128), bool)
    for y in range(64):
        a, b = int(y * 1.4), max(int(y * 1.4) + 1, int((y + 1) * 1.4))
        fr = face[a:b].mean(0) >= 0.5
        ur = under[a:b].mean(0) >= 0.5
        for x in range(128):
            lcd[y, x] = fr[x] or (ur[x] and (x + y) % 2 == 0)
    return lcd


def pack(lcd):
    out = bytearray(1024)
    for p in range(8):
        for x in range(128):
            v = 0
            for bit in range(8):
                v |= int(lcd[p * 8 + bit, x]) << bit
            out[p * 128 + x] = v
    return bytes(out)


def main():
    face, under = render()
    lcd = to_lcd(face, under)
    open(sys.argv[1], "wb").write(pack(lcd))
    img = np.where(lcd, 25, 205).astype("uint8")
    Image.fromarray(img).resize((128 * 5, int(64 * 5 * 1.4)), Image.NEAREST).save(sys.argv[2])


main()
