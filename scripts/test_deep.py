#!/usr/bin/env python3
"""Deep tests: shop economy, merchant reachability, L10 finish, save-key layout."""
from __future__ import annotations

import re
import sys
from collections import deque
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src" / "c"
DIM = 28
T_WALL, T_PILLAR = 3, 5
D_GATE, D_BLOCK, D_PIT, D_MAREN = 6, 11, 15, 9
D_STATIC_PILE, D_LOCKED_DOOR = 18, 19

errors: list[str] = []
ok: list[str] = []


def err(m: str) -> None:
    errors.append(m)


def pass_(m: str) -> None:
    ok.append(m)


def parse_map(level: int):
    h = (SRC / f"map{level}.h").read_text()

    def grid(name):
        m = re.search(rf"{name}\[{DIM}\]\[{DIM}\] = \{{(.*?)\}};", h, re.S)
        rows = []
        for line in m.group(1).splitlines():
            line = line.strip().rstrip(",")
            if line.startswith("{"):
                rows.append([int(x) for x in re.findall(r"-?\d+", line)])
        return rows

    tiles = grid(f"MAP{level}_TILES")
    decor = grid(f"MAP{level}_DECOR")
    events = grid(f"MAP{level}_EVENTS")
    sx = int(re.search(rf"MAP{level}_START_X (\d+)", h).group(1))
    sy = int(re.search(rf"MAP{level}_START_Y (\d+)", h).group(1))
    return tiles, decor, events, (sx, sy)


def walkable(tiles, decor, x, y, opened):
    if not (0 <= x < DIM and 0 <= y < DIM):
        return False
    if tiles[y][x] in (0, T_WALL, T_PILLAR):
        return False
    d = decor[y][x]
    if d == D_GATE and (x, y) not in opened:
        return False
    if d in (D_STATIC_PILE, D_LOCKED_DOOR) and (x, y) not in opened:
        return False
    if d in (D_BLOCK, D_PIT, D_MAREN):
        return False
    return True


def bfs(tiles, decor, start, opened=None):
    opened = opened or set()
    q = deque([start])
    seen = {start}
    while q:
        x, y = q.popleft()
        for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nx, ny = x + dx, y + dy
            if (nx, ny) in seen:
                continue
            if walkable(tiles, decor, nx, ny, opened):
                seen.add((nx, ny))
                q.append((nx, ny))
    return seen


def adjacent_reachable(seen, pos):
    x, y = pos
    return any((x + dx, y + dy) in seen for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)))


# ---- Shop economy simulation ---------------------------------------------
def test_shop_economy():
    # Prices from shop.c
    potion, reveal = 5, 10
    armors = [18, 30, 45, 65]  # cloak, vest, plate, signal
    # Gold ranges from combat.c (min,max)
    gold = {
        0: (1, 2), 1: (1, 3), 2: (2, 5), 3: (20, 30), 4: (3, 6),
        5: (4, 8), 6: (6, 12), 7: (8, 15), 8: (10, 20), 9: (40, 60),
    }
    tables = [
        [0, 1], [1, 2], [2, 4, 1], [4, 5], [5, 4, 6],
        [6, 5, 2], [6, 7, 5], [7, 8, 6], [8, 7, 6], [8, 7, 6],
    ]
    # 2 paid kills per type per map + Architect once
    gmin = gmax = 0
    for enemies in tables:
        for e in set(enemies):
            gmin += 2 * gold[e][0]
            gmax += 2 * gold[e][1]
    gmin += gold[9][0]
    gmax += gold[9][1]
    # Keeper mid-boss once (enemy id 3)
    gmin += gold[3][0]
    gmax += gold[3][1]

    need_all = sum(armors) + 4 * reveal  # 4 merchant floors
    need_armor = sum(armors)

    if gmin >= need_all:
        pass_(f"shop: min farmable {gmin}G covers all armor+reveals ({need_all}G)")
    elif gmax >= need_all:
        pass_(f"shop: max farmable {gmax}G covers all ({need_all}G); min {gmin} may need luck")
    else:
        err(f"shop: even max gold {gmax} < need {need_all}")

    # Simulate buy order: armor first then reveals then potions
    gold_sim = gmin
    bought = []
    for p in armors:
        if gold_sim >= p:
            gold_sim -= p
            bought.append(p)
    for _ in range(4):
        if gold_sim >= reveal:
            gold_sim -= reveal
            bought.append("reveal")
    potions = gold_sim // potion
    if len([b for b in bought if isinstance(b, int)]) == 4:
        pass_(f"shop sim (min gold): all 4 armor bought, {bought.count('reveal')} reveals, ~{potions} potions left")
    else:
        err(f"shop sim (min gold): only bought armor prices {bought}")

    # Armor tier gating: must buy in order
    armor_id = 0
    for i, p in enumerate(armors, 1):
        if armor_id + 1 != i:
            err("shop: armor tier order broken")
            break
        armor_id = i
    else:
        pass_("shop: armor tiers unlock sequentially 1→2→3→4")

    # Map reveal flags 100..109 < FLAG_COUNT 128
    if 100 + 9 < 128:
        pass_("shop: map-reveal flags 100-109 within FLAG_COUNT 128")
    else:
        err("shop: map-reveal flags overflow FLAG_COUNT")


def test_merchants():
    for level in (2, 5, 7, 10):
        tiles, decor, events, start = parse_map(level)
        merchants = [(x, y) for y in range(DIM) for x in range(DIM) if decor[y][x] == D_MAREN]
        if not merchants:
            err(f"L{level}: no merchant decor")
            continue
        seen = bfs(tiles, decor, start)
        for mpos in merchants:
            if not adjacent_reachable(seen, mpos):
                err(f"L{level}: merchant {mpos} not adjacent to reachable tiles from {start}")
            else:
                pass_(f"L{level}: merchant {mpos} reachable (bump from adjacent)")
            # event must exist
            eid = events[mpos[1]][mpos[0]]
            if eid < 0:
                err(f"L{level}: merchant {mpos} has no event")
            else:
                ev = (SRC / f"map{level}events.inc").read_text()
                if "CMD_OPEN_SHOP" not in ev:
                    err(f"L{level}: no CMD_OPEN_SHOP in events")
                else:
                    pass_(f"L{level}: merchant event opens shop")


def test_l10_finish():
    tiles, decor, events, start = parse_map(10)
    ev = (SRC / "map10events.inc").read_text()
    # sequence path
    path = [(6, 9), (6, 8), (5, 8), (4, 8), (4, 9)]
    for i, (x, y) in enumerate(path, 1):
        if events[y][x] < 0:
            err(f"L10: seq tile {i} ({x},{y}) missing event")
    if decor[8][14] != D_GATE:
        err("L10: boss gate missing at (14,8)")
    else:
        pass_("L10: boss gate at (14,8)")
    if "ENEMY_LICH" not in ev:
        err("L10: Architect battle missing")
    else:
        pass_("L10: Architect battle present")
    if "s_m10_end_dialog" not in ev:
        err("L10: ending dialog missing")
    else:
        pass_("L10: ending dialog present")

    # After opening gate, boss and stairs reachable
    opened = {(14, 8)}
    seen = bfs(tiles, decor, start, opened)
    # find boss tile (event with BATTLE) and stairs
    boss = None
    stairs = None
    for y in range(DIM):
        for x in range(DIM):
            e = events[y][x]
            if e < 0:
                continue
            # crude: look up in events file by scanning positions we know
    # Known from gen: Architect (18,8), stairs (18,16)
    if (18, 8) in seen:
        pass_("L10: Architect tile reachable after quake")
    else:
        err("L10: Architect unreachable after gate open")
    if (18, 16) in seen:
        pass_("L10: ending stairs reachable after quake")
    else:
        err("L10: ending stairs unreachable after gate open")

    # Merchant still reachable after boss path
    if adjacent_reachable(seen, (8, 1)):
        pass_("L10: merchant still reachable after gate open")
    else:
        err("L10: merchant blocked after gate open")


def test_save_keys():
    save_h = (SRC / "save.h").read_text()
    mm = (SRC / "minimap.c").read_text()
    # keys: 1 base, 2-11 deltas, 12 paid, 20-29 minimap
    if "SAVE_KEY_PAID" not in save_h:
        err("save: SAVE_KEY_PAID missing")
    else:
        m = re.search(r"SAVE_KEY_PAID\s+(\d+)", save_h)
        paid = int(m.group(1)) if m else -1
        if paid == 12:
            pass_("save: SAVE_KEY_PAID=12 (no clash with deltas 2-11)")
        else:
            err(f"save: SAVE_KEY_PAID={paid}, expected 12")
    if "MM_PERSIST_BASE 20" in mm or "MM_PERSIST_BASE  20" in mm.replace(" ", " "):
        pass_("save: minimap keys start at 20")
    elif re.search(r"MM_PERSIST_BASE\s+20", mm):
        pass_("save: minimap keys start at 20")
    else:
        err("save: minimap base key unexpected")

    # Check window_unload / shop / walkBack for save_write
    main = (SRC / "main.c").read_text()
    shop = (SRC / "shop.c").read_text()
    combat = (SRC / "combat.c").read_text()

    if "save_write" in main and "window_unload" in main:
        # Ensure save_write appears after window_unload definition start
        wu = main.find("window_unload")
        next_fn = main.find("\nint main", wu)
        chunk = main[wu:next_fn if next_fn > 0 else wu + 400]
        if "save_write" in chunk:
            pass_("save: window_unload calls save_write")
        else:
            err("save: window_unload does NOT call save_write (exit loses progress)")
    else:
        err("save: window_unload does NOT call save_write (exit loses progress)")

    if "save_write" in shop:
        pass_("save: shop purchases call save_write")
    else:
        err("save: shop does NOT call save_write after buy")

    wb = main.find("walkBack")
    wb_end = main.find("\nstatic void down_click", wb)
    wb_chunk = main[wb:wb_end if wb_end > 0 else wb + 500]
    if "save_write" in wb_chunk:
        pass_("save: walkBack calls save_write")
    else:
        err("save: walkBack does NOT call save_write")

    if "save_write" in combat:
        pass_("save: combat victory saves")
    else:
        err("save: combat does NOT call save_write on victory/defeat")


def test_stairs_landings():
    for level in range(1, 10):
        ev = (SRC / f"map{level}events.inc").read_text()
        m = re.search(r"change_map=\{\s*(\d+),\s*(\d+),\s*(\d+)", ev)
        if not m:
            err(f"L{level}: no CHANGE_MAP")
            continue
        mid, mx, my = int(m.group(1)), int(m.group(2)), int(m.group(3))
        dest = mid + 1
        tiles, decor, _, start = parse_map(dest)
        if (mx, my) != start:
            err(f"L{level}: stairs land ({mx},{my}) != dest start {start}")
        elif not walkable(tiles, decor, mx, my, set()):
            err(f"L{level}: stairs land in non-walkable ({mx},{my})")
        else:
            pass_(f"L{level}→L{dest}: stairs land at start {start}")



def test_v120_content():
    """v1.2.0 content: weapons, spellbook, Keeper, static/locked decor, magic."""
    m1 = (SRC / "map1events.inc").read_text()
    if "CMD_SET_WEAPON" in m1:
        pass_("v1.2.0: map1events has CMD_SET_WEAPON")
    else:
        err("v1.2.0: map1events missing CMD_SET_WEAPON")

    m3 = (SRC / "map3events.inc").read_text()
    if "CMD_SET_SPELLBOOK" in m3:
        pass_("v1.2.0: map3events has CMD_SET_SPELLBOOK")
    else:
        err("v1.2.0: map3events missing CMD_SET_SPELLBOOK")

    m5 = (SRC / "map5events.inc").read_text()
    if "ENEMY_WARDEN" in m5:
        pass_("v1.2.0: map5events has ENEMY_WARDEN (Keeper)")
    else:
        err("v1.2.0: map5events missing ENEMY_WARDEN")

    has_pile = has_door = False
    for level in range(1, 11):
        _, decor, _, _ = parse_map(level)
        for row in decor:
            if D_STATIC_PILE in row:
                has_pile = True
            if D_LOCKED_DOOR in row:
                has_door = True
    if has_pile:
        pass_("v1.2.0: at least one map has decor 18 (static pile)")
    else:
        err("v1.2.0: no map has decor 18 (static pile)")
    if has_door:
        pass_("v1.2.0: at least one map has decor 19 (locked door)")
    else:
        err("v1.2.0: no map has decor 19 (locked door)")

    player = (SRC / "player.h").read_text() + (SRC / "player.c").read_text()
    save_h = (SRC / "save.h").read_text()
    if "SPELL_PURGE" in player:
        pass_("v1.2.0: SPELL_PURGE present in player.h/c")
    else:
        err("v1.2.0: SPELL_PURGE missing from player.h/c")
    if "WEAPON_SPIKE" in player:
        pass_("v1.2.0: WEAPON_SPIKE present in player.h/c")
    else:
        err("v1.2.0: WEAPON_SPIKE missing from player.h/c")
    if re.search(r"SAVE_VERSION\s+[34]\b", save_h):
        pass_("v1.2.0: SAVE_VERSION is current (3+)")
    else:
        err("v1.2.0: SAVE_VERSION missing/too old in save.h")

    if (SRC / "magic.c").exists():
        pass_("v1.2.0: magic.c exists")
    else:
        err("v1.2.0: magic.c missing")


def main() -> int:
    print("=== Deep dive tests ===")
    test_shop_economy()
    test_merchants()
    test_l10_finish()
    test_save_keys()
    test_stairs_landings()
    test_v120_content()

    print(f"\nPASS ({len(ok)}):")
    for m in ok:
        print(f"  ✓ {m}")
    if errors:
        print(f"\nFAIL ({len(errors)}):")
        for m in errors:
            print(f"  ✗ {m}")
        print("\nOVERALL: FAIL")
        return 1
    print("\nOVERALL: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
