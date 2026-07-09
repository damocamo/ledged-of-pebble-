#!/usr/bin/env python3
"""Generate Rebble Appstore assets for Legend of Pebble.

Outputs into LOP/store/:
  icons/icon_48.png, icon_144.png
  banner/marketing_banner.png   (720x320)
  screenshots/01..05.png        (200x228, unframed — store-ready)
  sprites/promo_*.png           (extra store art)
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT / "store"
REPORTS = ROOT / "reports"
MONSTERS = ROOT / "resources" / "images" / "monsters.png"
ATLAS = ROOT / "resources" / "images" / "Atlas.png"

# LOP palette
BLACK = (0, 0, 0, 255)
AMBER_D = (170, 85, 0, 255)
AMBER = (255, 170, 0, 255)
TEAL_D = (0, 85, 170, 255)
TEAL = (0, 255, 170, 255)
GREY = (170, 170, 170, 255)
WHITE = (255, 255, 255, 255)
STONE = (200, 150, 100, 255)
STONE_D = (120, 70, 40, 255)


def nearest_font(size: int):
    for name in (
        "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/Library/Fonts/Courier New Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    ):
        p = Path(name)
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def scale_nn(im: Image.Image, factor: int) -> Image.Image:
    w, h = im.size
    return im.resize((w * factor, h * factor), Image.NEAREST)


def crop_monster(slot: int) -> Image.Image:
    """Crop a monster frame with true alpha (palette index 0 → transparent).

    Index 1 is also RGB black but is the outline — keep it opaque.
    """
    sheet = Image.open(MONSTERS)
    if sheet.mode != "P":
        sheet = sheet.convert("P")
    frame = sheet.crop((slot * 58, 0, (slot + 1) * 58, 63))
    # Build RGBA manually so only index 0 is transparent.
    pal = frame.getpalette() or []
    src = frame.load()
    rgba = Image.new("RGBA", frame.size)
    dst = rgba.load()
    w, h = frame.size
    for y in range(h):
        for x in range(w):
            i = src[x, y]
            if i == 0:
                dst[x, y] = (0, 0, 0, 0)
            else:
                r = pal[i * 3] if len(pal) > i * 3 else 0
                g = pal[i * 3 + 1] if len(pal) > i * 3 + 1 else 0
                b = pal[i * 3 + 2] if len(pal) > i * 3 + 2 else 0
                dst[x, y] = (r, g, b, 255)
    return rgba


def make_icons():
    """48x48 and 144x144 icons from real in-game dungeon footage + sprite.

    Uses a perspective corridor crop (not a flat far-wall brick plate), pastes
    a combat enemy at mid-scale, and nearest-neighbor scales so it reads like
    a Pebble screenshot thumbnail.
    """
    # Prefer deep corridor with void; fall back to explore / title.
    for candidate in (
        REPORTS / "04_level10.png",
        REPORTS / "01_compass.png",
        REPORTS / "00_title.png",
    ):
        if candidate.exists():
            src_path = candidate
            break
    else:
        raise SystemExit("FAIL: no report screenshots for icon source")

    shot = Image.open(src_path).convert("RGBA")
    sw, sh = shot.size
    # Square crop that keeps side-wall perspective + vanishing void.
    # Inset past the compass HUD (top-right) and bottom chrome.
    side = min(sw - 16, sh - 40)
    left = (sw - side) // 2
    top = 18
    corridor = shot.crop((left, top, left + side, top + side))

    # Compose at native 48px, then NN-scale to 144 (chunky Pebble pixels).
    base = 48
    scene = corridor.resize((base, base), Image.NEAREST)

    # Combat-style enemy in the corridor (gear spider). Keep it ~55% tall
    # so the hallway depth still reads behind the sprite.
    enemy = crop_monster(5)
    eh = int(base * 0.55)
    ew = max(1, int(enemy.size[0] * eh / enemy.size[1]))
    enemy = enemy.resize((ew, eh), Image.NEAREST)
    ex = (base - ew) // 2
    ey = base - eh - 3
    scene.paste(enemy, (ex, ey), enemy)

    # Tiny compass pip (matches in-game HUD) — drawn, not scaled mush.
    d = ImageDraw.Draw(scene)
    cx0, cy0, cx1, cy1 = base - 11, 2, base - 2, 11
    d.rectangle([cx0, cy0, cx1, cy1], fill=BLACK, outline=WHITE)
    d.ellipse([cx0 + 1, cy0 + 1, cx1 - 1, cy1 - 1], outline=WHITE)
    d.line([(cx0 + 5, cy0 + 5), (cx1 - 2, cy0 + 3)], fill=(255, 0, 0, 255))

    # 1px amber frame; corners stay opaque (full-bleed store icon).
    d.rectangle([0, 0, base - 1, base - 1], outline=AMBER)

    out = STORE / "icons"
    out.mkdir(parents=True, exist_ok=True)
    scene.save(out / "icon_48.png")
    scene.resize((144, 144), Image.NEAREST).save(out / "icon_144.png")
    print("icons ok")


def make_banner():
    """720x320 marketing banner."""
    W, H = 720, 320
    im = Image.new("RGBA", (W, H), BLACK)
    d = ImageDraw.Draw(im)

    # stone brick background
    for y in range(0, H, 20):
        for x in range(0, W, 40):
            ox = (y // 20 % 2) * 20
            d.rectangle([x + ox, y, x + ox + 38, y + 18], fill=STONE_D, outline=AMBER_D)

    # teal rift streak
    for i in range(40):
        x = 280 + i * 3
        y = 40 + i * 5
        d.ellipse([x, y, x + 18, y + 40], fill=TEAL_D if i % 2 else TEAL)

    # left: framed title screenshot
    title = Image.open(REPORTS / "00_title.png").convert("RGBA")
    shot = title.resize((160, 182), Image.NEAREST)
    frame = Image.new("RGBA", (168, 190), AMBER)
    frame.paste(shot, (4, 4))
    im.paste(frame, (36, 64), frame)

    # right: merchant + architect sprites
    merchant = scale_nn(crop_monster(10), 3)
    architect = scale_nn(crop_monster(9), 3)
    im.paste(merchant, (520, 40), merchant)
    im.paste(architect, (580, 120), architect)

    # title text
    font_lg = nearest_font(42)
    font_sm = nearest_font(20)
    font_tiny = nearest_font(14)

    def outline_text(pos, text, font, fill=WHITE, outline=BLACK):
        x, y = pos
        for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, 1)):
            d.text((x + dx, y + dy), text, font=font, fill=outline)
        d.text(pos, text, font=font, fill=fill)

    outline_text((220, 50), "LEGEND OF PEBBLE", font_lg, AMBER)
    outline_text((220, 110), "A watch away from home.", font_sm, TEAL)
    outline_text((220, 150), "10 levels  ·  Merchants  ·  Bosses", font_tiny, GREY)
    outline_text((220, 250), "Fork of Dungeon Escape by Brian Ouellette", font_tiny, WHITE)
    outline_text((220, 275), "Pebble Time 2  ·  Round 2", font_tiny, GREY)

    out = STORE / "banner"
    out.mkdir(parents=True, exist_ok=True)
    im.convert("RGB").save(out / "marketing_banner.png")
    print("banner ok")


def make_screenshots():
    """Curate up to 5 unframed 200x228 screenshots for the listing."""
    out = STORE / "screenshots"
    out.mkdir(parents=True, exist_ok=True)

    # Prefer real captures; synthesize combat/shop if missing.
    picks = [
        ("01_title.png", REPORTS / "00_title.png"),
        ("02_explore.png", REPORTS / "01_compass.png"),
        ("03_inventory.png", REPORTS / "03_menu_labels.png"),
        ("04_core.png", REPORTS / "04_level10.png"),
    ]
    for name, src in picks:
        if src.exists():
            Image.open(src).convert("RGBA").save(out / name)

    # Synthesize combat screenshot: fill deep voids so the enemy isn't in a
    # black hole, then paste a transparent gear-spider sprite.
    combat = Image.open(REPORTS / "01_compass.png").convert("RGBA")
    px = combat.load()
    cw, ch = combat.size
    stone = (200, 150, 100, 255)
    stone_d = (120, 70, 40, 255)
    for y in range(ch):
        for x in range(cw):
            r, g, b, a = px[x, y]
            if r < 20 and g < 20 and b < 20:
                px[x, y] = stone if (x + y) % 3 else stone_d
    enemy = scale_nn(crop_monster(5), 2)  # gear spider
    ex = (cw - enemy.size[0]) // 2
    ey = (ch - enemy.size[1]) // 2 - 8
    combat.paste(enemy, (ex, ey), enemy)
    d = ImageDraw.Draw(combat)
    font = nearest_font(12)
    d.rectangle([8, 8, 192, 32], fill=(0, 0, 0, 220))
    d.text((18, 12), "GEAR SPIDER", font=font, fill=AMBER)
    d.rectangle([8, 192, 192, 220], fill=(0, 0, 0, 220))
    d.text((22, 200), "ATK   POT   RUN", font=font, fill=TEAL)
    combat.save(out / "05_combat.png")

    print("screenshots:", sorted(p.name for p in out.glob("*.png")))


def make_promo_sprites():
    """Extra store art: merchant card, rift watch, enemy lineup.

    All promo sheets use a transparent background (not opaque black).
    """
    out = STORE / "sprites"
    out.mkdir(parents=True, exist_ok=True)
    clear = (0, 0, 0, 0)

    # Merchant promo — transparent canvas, brick frame only around edges
    card = Image.new("RGBA", (320, 320), clear)
    d = ImageDraw.Draw(card)
    # brick border (outer 24px), leave center transparent behind the sprite
    for y in range(0, 320, 16):
        for x in range(0, 320, 32):
            ox = (y // 16 % 2) * 16
            rx0, ry0 = x + ox, y
            rx1, ry1 = x + ox + 30, y + 14
            # only draw bricks near the frame edge
            if rx0 < 24 or ry0 < 24 or rx1 > 296 or ry1 > 296:
                d.rectangle([rx0, ry0, rx1, ry1], fill=STONE_D, outline=AMBER_D)
    m = scale_nn(crop_monster(10), 4)
    card.paste(m, (44, 40), m)
    font = nearest_font(22)
    # outlined label so it reads on any store background
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        d.text((40 + dx, 280 + dy), "THE MERCHANT", font=font, fill=BLACK)
    d.text((40, 280), "THE MERCHANT", font=font, fill=AMBER)
    card.save(out / "promo_merchant.png")

    # Enemy lineup strip — transparent between sprites
    strip = Image.new("RGBA", (58 * 6 * 2, 63 * 2), clear)
    for i, slot in enumerate([0, 4, 5, 6, 8, 9]):
        spr = scale_nn(crop_monster(slot), 2)
        strip.paste(spr, (i * 58 * 2, 0), spr)
    strip.save(out / "promo_enemies.png")

    # Watch / rift emblem — already transparent outside the circle
    emblem = Image.new("RGBA", (256, 256), clear)
    d = ImageDraw.Draw(emblem)
    d.ellipse([20, 20, 236, 236], fill=STONE_D, outline=AMBER, width=6)
    d.ellipse([56, 56, 200, 200], fill=BLACK, outline=TEAL, width=5)
    d.line([(128, 128), (128, 70)], fill=AMBER, width=5)
    d.line([(128, 128), (170, 128)], fill=TEAL, width=5)
    d.ellipse([122, 122, 134, 134], fill=WHITE)
    for p in [(60, 80), (190, 70), (70, 180), (185, 175)]:
        d.ellipse([p[0], p[1], p[0] + 6, p[1] + 6], fill=TEAL)
    emblem.save(out / "promo_watch_rift.png")

    print("promo sprites ok")


def verify_transparency():
    """Fail loudly if game sprites or promo art lose transparency."""
    monsters = Image.open(MONSTERS)
    if monsters.mode != "P" or monsters.info.get("transparency") != 0:
        raise SystemExit(
            f"FAIL: {MONSTERS} must be palette PNG with transparency=0 "
            f"(got mode={monsters.mode} transparency={monsters.info.get('transparency')})"
        )
    # Promo sheets must have real alpha (not a solid black plate)
    for name in ("promo_enemies.png", "promo_merchant.png", "promo_watch_rift.png"):
        p = STORE / "sprites" / name
        im = Image.open(p).convert("RGBA")
        alpha0 = sum(1 for px in im.getdata() if px[3] == 0)
        if alpha0 < 100:
            raise SystemExit(f"FAIL: {p} has almost no transparent pixels ({alpha0})")
    print("transparency verify ok")


def main():
    STORE.mkdir(parents=True, exist_ok=True)
    make_icons()
    make_banner()
    make_screenshots()
    make_promo_sprites()
    verify_transparency()
    print(f"Wrote assets under {STORE}")


if __name__ == "__main__":
    main()
