#!/usr/bin/env python3
"""Split resources/images/monsters.png into per-monster PNGs.

The full 638x63 sheet costs ~20KB of heap if loaded as one GBitmap, which no
longer fits alongside the tile atlas. Combat/shop instead load one 58x63
slice (~1.9KB) on demand. Run this after regenerating monsters.png.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHEET = ROOT / "resources" / "images" / "monsters.png"
SLOT_W, SLOT_H = 58, 63
SLOTS = 11  # 10 enemies + merchant


def main() -> None:
    im = Image.open(SHEET)
    assert im.mode == "P" and im.info.get("transparency") == 0, (
        f"monsters.png must be palette PNG with transparency=0 "
        f"(got mode={im.mode} transparency={im.info.get('transparency')})"
    )
    w, h = im.size
    assert w >= SLOTS * SLOT_W and h >= SLOT_H, f"sheet too small: {im.size}"

    for slot in range(SLOTS):
        box = im.crop((slot * SLOT_W, 0, (slot + 1) * SLOT_W, SLOT_H))
        out = SHEET.parent / f"monster_{slot:02d}.png"
        box.save(out, transparency=0, optimize=True)
        print(f"wrote {out.name} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
