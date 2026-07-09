#!/usr/bin/env python3
"""Generate Legend of Pebble monster spritesheet (11 frames: 10 enemies + merchant).

Palette (indexed, matching existing monsters.png):
  0 black/transparent bg
  1 black outline
  2 dark amber   (170, 85, 0)
  3 bright amber (255,170, 0)
  4 grey         (170,170,170)
  5 dark teal    (0, 85,170)
  6 bright teal  (0,255,170)
  7 white
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image

W, H = 58, 63
N = 11
OUT = Path(__file__).resolve().parents[1] / "resources" / "images" / "monsters.png"
SRC = OUT  # reuse existing palette + enemy frames

# palette RGB triples (index order)
PAL = [
    (0, 0, 0),
    (0, 0, 0),
    (170, 85, 0),
    (255, 170, 0),
    (170, 170, 170),
    (0, 85, 170),
    (0, 255, 170),
    (255, 255, 255),
]


def blank():
    return [[0] * W for _ in range(H)]


def put(g, x, y, c):
    if 0 <= x < W and 0 <= y < H and c:
        g[y][x] = c


def fill(g, pts, c):
    for x, y in pts:
        put(g, x, y, c)


def rect(g, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(g, x, y, c)


def oval(g, cx, cy, rx, ry, c, fill_c=None):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            dx = (x - cx) / max(rx, 1)
            dy = (y - cy) / max(ry, 1)
            if dx * dx + dy * dy <= 1.05:
                put(g, x, y, fill_c if fill_c is not None else c)
    if fill_c is not None and fill_c != c:
        # outline: overwrite rim with outline color
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                dx = (x - cx) / max(rx, 1)
                dy = (y - cy) / max(ry, 1)
                d = dx * dx + dy * dy
                if 0.72 <= d <= 1.05:
                    put(g, x, y, c)


def outline_silhouette(g, body_colors=(2, 3, 4, 5, 6, 7)):
    """Add black outline around any non-bg pixel."""
    body = {(x, y) for y in range(H) for x in range(W) if g[y][x] in body_colors}
    for x, y in list(body):
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H and g[ny][nx] == 0:
                put(g, nx, ny, 1)


def copy_slot(src: Image.Image, slot: int):
    g = blank()
    for y in range(H):
        for x in range(W):
            g[y][x] = src.getpixel((slot * W + x, y))
    return g


# ---- New sprites -----------------------------------------------------------

def clockbone():
    """Skeleton with clock-face chest and gear joints."""
    g = blank()
    # skull
    oval(g, 29, 12, 9, 8, 1, 4)
    rect(g, 24, 10, 26, 12, 1)  # eye sockets
    rect(g, 32, 10, 34, 12, 1)
    put(g, 25, 11, 6)
    put(g, 33, 11, 6)
    rect(g, 27, 14, 31, 15, 1)  # nose/mouth
    for x in range(25, 34):
        put(g, x, 16, 4)
    # spine / ribs
    for y in range(20, 38):
        put(g, 29, y, 4)
        put(g, 28, y, 1)
        put(g, 30, y, 1)
    for y in (22, 26, 30, 34):
        for dx in range(1, 8):
            put(g, 29 - dx, y, 4)
            put(g, 29 + dx, y, 4)
            put(g, 29 - dx, y - 1, 1)
            put(g, 29 + dx, y - 1, 1)
    # clock chest
    oval(g, 29, 28, 7, 7, 1, 2)
    oval(g, 29, 28, 5, 5, 3, 3)
    put(g, 29, 28, 7)  # center pin
    for i in range(5):
        put(g, 29, 24 + i, 1)  # hour hand
    for i in range(4):
        put(g, 29 + i, 28, 5)  # minute hand teal
    # arms with gear elbows
    for x, y in [(18, 24), (19, 25), (20, 26), (21, 27), (22, 28)]:
        put(g, x, y, 4)
    for x, y in [(40, 24), (39, 25), (38, 26), (37, 27), (36, 28)]:
        put(g, x, y, 4)
    oval(g, 17, 30, 3, 3, 1, 2)
    oval(g, 41, 30, 3, 3, 1, 2)
    for i in range(4):
        put(g, 17, 33 + i, 4)
        put(g, 41, 33 + i, 4)
    # pelvis + legs
    rect(g, 24, 38, 34, 40, 4)
    for y in range(41, 58):
        put(g, 25, y, 4)
        put(g, 33, y, 4)
        put(g, 24, y, 1)
        put(g, 34, y, 1)
    # gear knees
    oval(g, 25, 48, 3, 3, 1, 3)
    oval(g, 33, 48, 3, 3, 1, 3)
    put(g, 25, 48, 6)
    put(g, 33, 48, 6)
    outline_silhouette(g)
    return g


def gear_spider():
    """Eight-legged spider with a gear abdomen."""
    g = blank()
    # abdomen gear
    oval(g, 30, 36, 14, 12, 1, 2)
    oval(g, 30, 36, 10, 8, 3, 3)
    # gear teeth
    for ang in range(0, 360, 45):
        import math
        rad = math.radians(ang)
        tx = int(30 + 13 * math.cos(rad))
        ty = int(36 + 11 * math.sin(rad))
        rect(g, tx - 1, ty - 1, tx + 1, ty + 1, 2)
    # hub
    oval(g, 30, 36, 4, 4, 1, 5)
    put(g, 30, 36, 6)
    # cephalothorax
    oval(g, 30, 20, 8, 6, 1, 2)
    # eyes (teal cluster)
    for x, y in [(26, 18), (28, 17), (30, 17), (32, 17), (34, 18), (27, 20), (33, 20)]:
        put(g, x, y, 6)
        put(g, x, y - 1, 1)
    # fangs
    put(g, 28, 24, 7)
    put(g, 32, 24, 7)
    put(g, 28, 25, 4)
    put(g, 32, 25, 4)
    # legs (4 per side)
    legs_l = [
        [(18, 16), (12, 12), (6, 10), (2, 12)],
        [(16, 22), (8, 22), (3, 24), (1, 28)],
        [(16, 30), (8, 34), (3, 40), (1, 46)],
        [(18, 38), (12, 46), (8, 54), (6, 58)],
    ]
    legs_r = [
        [(42, 16), (48, 12), (52, 10), (55, 12)],
        [(44, 22), (50, 22), (54, 24), (56, 28)],
        [(44, 30), (50, 34), (54, 40), (56, 46)],
        [(42, 38), (48, 46), (50, 54), (52, 58)],
    ]
    for chain in legs_l + legs_r:
        for i, (x, y) in enumerate(chain):
            put(g, x, y, 3 if i % 2 == 0 else 2)
            put(g, x + 1, y, 1)
            put(g, x, y + 1, 1)
            if i:
                px, py = chain[i - 1]
                steps = max(abs(x - px), abs(y - py), 1)
                for s in range(steps + 1):
                    lx = px + (x - px) * s // steps
                    ly = py + (y - py) * s // steps
                    put(g, lx, ly, 2)
                    put(g, lx, ly + 1, 1)
    outline_silhouette(g)
    return g


def static_wraith():
    """Glitchy floating wraith — jagged silhouette, teal static."""
    g = blank()
    # head / hood
    oval(g, 29, 14, 10, 10, 1, 4)
    rect(g, 22, 8, 36, 12, 4)
    # face void
    rect(g, 24, 12, 34, 18, 1)
    # glowing eyes
    rect(g, 25, 14, 27, 15, 6)
    rect(g, 31, 14, 33, 15, 6)
    put(g, 26, 14, 7)
    put(g, 32, 14, 7)
    # body cloak with static noise bands
    for y in range(22, 56):
        width = 8 + (y - 22) // 4
        # jagged edges
        jag = (y * 3) % 5 - 2
        for x in range(29 - width + jag, 29 + width + jag):
            # alternating static stripes
            if (x + y) % 5 == 0:
                put(g, x, y, 6)
            elif (x + y) % 3 == 0:
                put(g, x, y, 5)
            else:
                put(g, x, y, 4)
    # arms raised
    for i in range(12):
        put(g, 14 + i // 2, 24 + i, 4)
        put(g, 15 + i // 2, 24 + i, 5)
        put(g, 44 - i // 2, 24 + i, 4)
        put(g, 43 - i // 2, 24 + i, 5)
    # sparkles / glitch pixels
    for x, y in [(10, 20), (12, 30), (8, 40), (48, 22), (50, 34), (46, 48),
                 (20, 50), (38, 52), (29, 58), (22, 8), (36, 6)]:
        put(g, x, y, 6)
        put(g, x + 1, y, 7)
    outline_silhouette(g, body_colors=(2, 3, 4, 5, 6, 7))
    return g


def quartz_golem():
    """Blocky crystal golem — amber quartz with teal veins."""
    g = blank()
    # head crystal
    pts = [(29, 4), (22, 12), (24, 18), (34, 18), (36, 12)]
    # fill head polygon roughly
    for y in range(4, 19):
        for x in range(20, 39):
            if abs(x - 29) + (y - 4) * 0.6 < 12 and y + abs(x - 29) // 2 < 22:
                put(g, x, y, 3 if (x + y) % 3 else 2)
    # eyes
    rect(g, 24, 11, 26, 13, 6)
    rect(g, 32, 11, 34, 13, 6)
    put(g, 25, 12, 7)
    put(g, 33, 12, 7)
    # torso blocks
    rect(g, 18, 20, 40, 42, 2)
    rect(g, 20, 22, 38, 40, 3)
    # teal crystal veins
    for y in range(22, 40):
        put(g, 29, y, 6 if y % 2 == 0 else 5)
    for x in range(22, 37):
        put(g, x, 30, 5)
    put(g, 24, 26, 6)
    put(g, 34, 34, 6)
    # shoulders
    rect(g, 12, 20, 18, 28, 2)
    rect(g, 40, 20, 46, 28, 2)
    rect(g, 13, 21, 17, 27, 3)
    rect(g, 41, 21, 45, 27, 3)
    # arms
    rect(g, 10, 28, 16, 48, 2)
    rect(g, 42, 28, 48, 48, 2)
    rect(g, 11, 29, 15, 47, 3)
    rect(g, 43, 29, 47, 47, 3)
    # fists
    rect(g, 8, 48, 18, 56, 2)
    rect(g, 40, 48, 50, 56, 2)
    rect(g, 9, 49, 17, 55, 4)
    rect(g, 41, 49, 49, 55, 4)
    # legs
    rect(g, 20, 42, 27, 60, 2)
    rect(g, 31, 42, 38, 60, 2)
    rect(g, 21, 43, 26, 59, 3)
    rect(g, 32, 43, 37, 59, 3)
    # feet
    rect(g, 18, 58, 28, 61, 2)
    rect(g, 30, 58, 40, 61, 2)
    outline_silhouette(g)
    return g


def rift_ogre():
    """Bulky horned ogre with rift-crack chest."""
    g = blank()
    # horns
    for i in range(8):
        put(g, 18 - i // 2, 10 - i, 2)
        put(g, 18 - i // 2, 11 - i, 3)
        put(g, 40 + i // 2, 10 - i, 2)
        put(g, 40 + i // 2, 11 - i, 3)
    # head
    oval(g, 29, 16, 12, 10, 1, 2)
    oval(g, 29, 16, 10, 8, 3, 3)
    # eyes
    rect(g, 23, 14, 26, 16, 1)
    rect(g, 32, 14, 35, 16, 1)
    put(g, 24, 15, 6)
    put(g, 33, 15, 6)
    # snarl
    rect(g, 25, 20, 33, 22, 1)
    for x in range(26, 33, 2):
        put(g, x, 21, 7)
    # massive torso
    oval(g, 29, 36, 16, 14, 1, 2)
    oval(g, 29, 36, 14, 12, 3, 3)
    # rift crack down chest
    for y in range(26, 48):
        put(g, 29, y, 6)
        put(g, 28, y, 5)
        put(g, 30, y, 5)
        if y % 3 == 0:
            put(g, 27, y, 6)
            put(g, 31, y, 6)
    # arms
    oval(g, 10, 34, 6, 10, 1, 2)
    oval(g, 48, 34, 6, 10, 1, 2)
    oval(g, 10, 34, 4, 8, 3, 3)
    oval(g, 48, 34, 4, 8, 3, 3)
    # fists
    oval(g, 10, 48, 7, 6, 1, 2)
    oval(g, 48, 48, 7, 6, 1, 2)
    # legs
    rect(g, 20, 48, 26, 60, 2)
    rect(g, 32, 48, 38, 60, 2)
    rect(g, 21, 49, 25, 59, 3)
    rect(g, 33, 49, 37, 59, 3)
    outline_silhouette(g)
    return g


def architect():
    """Final boss — tall hooded figure with geometric crown and staff."""
    g = blank()
    # crown / antennae geometry
    for i in range(10):
        put(g, 29, 2 + i, 3)
        put(g, 28, 2 + i, 1)
        put(g, 30, 2 + i, 1)
    rect(g, 22, 6, 36, 8, 3)
    rect(g, 20, 8, 38, 9, 2)
    put(g, 22, 5, 6)
    put(g, 36, 5, 6)
    put(g, 29, 1, 7)
    # hood
    oval(g, 29, 18, 12, 11, 1, 2)
    oval(g, 29, 18, 10, 9, 5, 5)
    # face plate
    rect(g, 24, 14, 34, 24, 1)
    rect(g, 25, 15, 33, 23, 4)
    # visor eyes
    rect(g, 26, 17, 28, 19, 6)
    rect(g, 30, 17, 32, 19, 6)
    put(g, 27, 18, 7)
    put(g, 31, 18, 7)
    # mouth grill
    for x in range(26, 33):
        put(g, x, 22, 1 if x % 2 else 6)
    # robe body
    for y in range(28, 60):
        half = 6 + (y - 28) // 3
        for x in range(29 - half, 30 + half):
            c = 2 if (x + y) % 4 == 0 else 5
            if abs(x - 29) < 3:
                c = 3
            put(g, x, y, c)
    # chest sigil
    oval(g, 29, 34, 5, 5, 1, 3)
    put(g, 29, 34, 6)
    for dx in (-2, 0, 2):
        put(g, 29 + dx, 34, 7 if dx == 0 else 6)
        put(g, 29, 34 + dx, 7 if dx == 0 else 6)
    # staff (left)
    for y in range(12, 60):
        put(g, 12, y, 3)
        put(g, 11, y, 1)
        put(g, 13, y, 1)
    oval(g, 12, 10, 4, 4, 1, 6)
    put(g, 12, 10, 7)
    # right hand gesture
    oval(g, 44, 36, 4, 4, 1, 4)
    for i in range(5):
        put(g, 46 + i, 34 - i, 6)
    outline_silhouette(g)
    return g


def merchant():
    """Friendly trader — cloak, satchel, teal coin pouch."""
    g = blank()
    # hood / head
    oval(g, 29, 14, 10, 9, 1, 2)
    oval(g, 29, 14, 8, 7, 3, 3)
    # face
    rect(g, 24, 12, 34, 18, 4)
    put(g, 26, 14, 6)
    put(g, 32, 14, 6)
    rect(g, 27, 17, 31, 18, 1)
    # body cloak
    for y in range(22, 56):
        half = 5 + (y - 22) // 4
        for x in range(29 - half, 30 + half):
            put(g, x, y, 2 if (x + y) % 3 else 3)
    # satchel strap
    for i in range(14):
        put(g, 22 + i // 2, 28 + i, 5)
        put(g, 23 + i // 2, 28 + i, 6)
    # satchel
    oval(g, 38, 42, 7, 6, 1, 2)
    oval(g, 38, 42, 5, 4, 3, 3)
    put(g, 38, 42, 6)
    # coin pouch (left)
    oval(g, 18, 40, 5, 5, 1, 3)
    put(g, 18, 40, 7)
    put(g, 17, 39, 6)
    put(g, 19, 39, 6)
    # arms
    for i in range(10):
        put(g, 16, 28 + i, 4)
        put(g, 42, 28 + i, 4)
    # smile of a trader — small teal badge on chest
    oval(g, 29, 32, 3, 3, 1, 6)
    put(g, 29, 32, 7)
    outline_silhouette(g)
    return g


def to_image(grids):
    # Build palette image. Index 0 is the transparent key (matches Dungeon Escape
    # monsters.png) so combat drawing skips the black box behind sprites.
    img = Image.new("P", (W * len(grids), H))
    flat = []
    for r, g, b in PAL:
        flat.extend([r, g, b])
    flat.extend([0] * (768 - len(flat)))
    img.putpalette(flat)
    img.info["transparency"] = 0
    px = img.load()
    for i, g in enumerate(grids):
        ox = i * W
        for y in range(H):
            for x in range(W):
                px[ox + x, y] = g[y][x]
    return img


def main():
    src = Image.open(SRC)
    # Prefer keeping existing enemy art; regenerate procedural ones if sheet is short.
    n_existing = src.size[0] // W
    grids = []
    for i in range(min(10, n_existing)):
        grids.append(copy_slot(src, i))
    # If sheet was the original 4-wide, regenerate the rest
    while len(grids) < 4:
        grids.append(blank())
    procedural = [clockbone, gear_spider, static_wraith, quartz_golem, rift_ogre, architect]
    for i, fn in enumerate(procedural):
        slot = 4 + i
        if slot >= len(grids):
            grids.append(fn())
    grids = grids[:10]
    grids.append(merchant())  # slot 10
    out = to_image(grids)
    # Explicit transparency=0 so pebble resource pack marks palette[0] as alpha=0.
    out.save(OUT, transparency=0)
    print(f"Wrote {OUT} ({out.size[0]}x{out.size[1]}, {len(grids)} sprites, transparency=0)")


if __name__ == "__main__":
    main()
