#!/usr/bin/env python3
"""Full integrity + solvability audit for Legend of Pebble levels."""
from __future__ import annotations

import re
import sys
from collections import deque
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src" / "c"
DIM = 28
T_FLOOR, T_FLOORR, T_WALL, T_PILLAR, T_SECRET = 1, 2, 3, 5, 6
D_CHEST, D_GATE, D_BLOCK, D_PLATE, D_TELE, D_PIT = 2, 6, 11, 12, 14, 15
D_STATIC_PILE, D_LOCKED_DOOR = 18, 19
ENEMY_COUNT = 10
SPRITE_SLOTS = 11  # 10 enemies + merchant portrait
errors: list[str] = []
warnings: list[str] = []


def err(msg: str) -> None:
    errors.append(msg)


def warn(msg: str) -> None:
    warnings.append(msg)


def parse_map(level: int):
    h = (SRC / f"map{level}.h").read_text()

    def grid(name):
        m = re.search(rf"{name}\[{DIM}\]\[{DIM}\] = \{{(.*?)\}};", h, re.S)
        if not m:
            err(f"L{level}: missing {name}")
            return [[0] * DIM for _ in range(DIM)]
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
    if d in (D_BLOCK, D_PIT):
        return False
    return True


def bfs(tiles, decor, start, opened, tele=None):
    tele = tele or {}
    q = deque([start])
    seen = {start}
    while q:
        x, y = q.popleft()
        if (x, y) in tele:
            nx, ny = tele[(x, y)]
            if (nx, ny) not in seen:
                seen.add((nx, ny))
                q.append((nx, ny))
        for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nx, ny = x + dx, y + dy
            if (nx, ny) in seen:
                continue
            if walkable(tiles, decor, nx, ny, opened):
                seen.add((nx, ny))
                q.append((nx, ny))
    return seen


def find(grid, val):
    return [(x, y) for y in range(DIM) for x in range(DIM) if grid[y][x] == val]


def parse_events(level: int):
    ev = (SRC / f"map{level}events.inc").read_text()
    count_m = re.search(rf"MAP{level}_EVENT_COUNT", ev)
    cmds = {}
    for m in re.finditer(
        rf"s_m{level}_ev(\d+)_cmds\[\] = \{{(.*?)\}};", ev, re.S
    ):
        cmds[int(m.group(1))] = m.group(2)
    return cmds, ev


def audit_level(level: int, starts: dict):
    tiles, decor, events, start = parse_map(level)
    cmds, _ev_text = parse_events(level)
    event_count = len(cmds)

    if not walkable(tiles, decor, start[0], start[1], set()):
        err(f"L{level}: start {start} not walkable")

    # event index bounds
    for y in range(DIM):
        for x in range(DIM):
            e = events[y][x]
            if e < -1 or e >= event_count:
                err(f"L{level}: event id {e} at ({x},{y}) out of range 0..{event_count-1}")

    gates = find(decor, D_GATE)
    blocks = find(decor, D_BLOCK)
    teles = find(decor, D_TELE)
    chests = find(decor, D_CHEST)

    tele = {}
    stair_evs, button_evs, alcove_evs, boss_evs = set(), set(), set(), set()
    gate_opens = {}
    change_maps = []

    for eid, body in cmds.items():
        if "BLINK" in body:
            mm = re.search(r"move_player=\{\s*(\d+),\s*(\d+)", body)
            if mm:
                dest = (int(mm.group(1)), int(mm.group(2)))
                for t in teles:
                    if events[t[1]][t[0]] == eid:
                        tele[t] = dest
                        if not walkable(tiles, decor, dest[0], dest[1], set(gates)):
                            # dest may be behind gate; check floor at least
                            if tiles[dest[1]][dest[0]] in (0, T_WALL, T_PILLAR):
                                err(f"L{level}: teleporter dest {dest} is wall")
        if "CHANGE_MAP" in body or "s_m10_end_dialog" in body:
            stair_evs.add(eid)
        if "A GATE OPENS" in body:
            button_evs.add(eid)
        if "THE GATE OPENS" in body:
            alcove_evs.add(eid)
        if "CMD_BATTLE" in body:
            boss_evs.add(eid)
            bm = re.search(r"battle=\{\s*(\w+)", body)
            if bm:
                name = bm.group(1)
                # resolve ENEMY_* enum order from combat.h mentally: 0..9
                enemy_ids = {
                    "ENEMY_RAT": 0, "ENEMY_BAT": 1, "ENEMY_GUARD": 2,
                    "ENEMY_WARDEN": 3, "ENEMY_SKELETON": 4, "ENEMY_SPIDER": 5,
                    "ENEMY_WRAITH": 6, "ENEMY_GOLEM": 7, "ENEMY_OGRE": 8,
                    "ENEMY_LICH": 9,
                }
                if name in enemy_ids and enemy_ids[name] >= ENEMY_COUNT:
                    err(f"L{level}: battle enemy {name} OOB")
                elif name not in enemy_ids:
                    err(f"L{level}: unknown enemy {name}")
        for gm in re.finditer(
            r"change_decor=\{\s*(\d+),\s*(\d+),\s*DECOR_NONE", body
        ):
            gx, gy = int(gm.group(1)), int(gm.group(2))
            if (gx, gy) in gates:
                gate_opens.setdefault(eid, []).append((gx, gy))
        cm = re.search(
            r"change_map=\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", body
        )
        if cm:
            change_maps.append(
                (eid, int(cm.group(1)), int(cm.group(2)), int(cm.group(3)), int(cm.group(4)))
            )

    stairs = [(x, y) for y in range(DIM) for x in range(DIM) if events[y][x] in stair_evs]
    buttons = [(x, y) for y in range(DIM) for x in range(DIM) if events[y][x] in button_evs]
    alcoves = [(x, y) for y in range(DIM) for x in range(DIM) if events[y][x] in alcove_evs]
    bosses = [(x, y) for y in range(DIM) for x in range(DIM) if events[y][x] in boss_evs]

    # CHANGE_MAP destinations
    for eid, mid, mx, my, facing in change_maps:
        if mid < 0 or mid > 9:
            err(f"L{level}: CHANGE_MAP to invalid map_id {mid}")
            continue
        dest_level = mid + 1
        if dest_level not in starts:
            # will be filled as we go; check tiles of dest
            dt, dd, _, ds = parse_map(dest_level)
            starts[dest_level] = ds
        else:
            dt, dd, _, _ = parse_map(dest_level)
        if not walkable(dt, dd, mx, my, set()):
            err(
                f"L{level}: stairs land at map{dest_level} ({mx},{my}) which is NOT walkable "
                f"(authored start is {starts.get(dest_level)})"
            )
        elif (mx, my) != starts.get(dest_level):
            warn(
                f"L{level}: stairs land at ({mx},{my}) but map{dest_level} start is "
                f"{starts.get(dest_level)}"
            )

    # plate links
    links = []
    pl = (SRC / "puzzle_links.inc").read_text()
    mid = level - 1
    if f"s_map{mid}_plates" in pl:
        block = re.search(rf"s_map{mid}_plates\[\] = \{{(.*?)\}};", pl, re.S)
        if block:
            for row in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", block.group(1)):
                links.append(tuple(map(int, row)))

    opened = set()
    bad = False
    seen = bfs(tiles, decor, start, opened, tele)

    for px, py, gx, gy in links:
        ok = False
        for bx, by in blocks:
            adj = [(bx + dx, by + dy) for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))]
            if not any(a in seen for a in adj):
                continue
            q = deque([(bx, by)])
            s = {(bx, by)}
            while q:
                x, y = q.popleft()
                if (x, y) == (px, py):
                    ok = True
                    break
                for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                    nx, ny = x + dx, y + dy
                    if (nx, ny) in s:
                        continue
                    if not (0 <= nx < DIM and 0 <= ny < DIM):
                        continue
                    if tiles[ny][nx] not in (T_FLOOR, T_FLOORR, T_SECRET):
                        continue
                    d = decor[ny][nx]
                    if d in (D_GATE, D_BLOCK, D_PIT, D_STATIC_PILE, D_LOCKED_DOOR) and (nx, ny) != (px, py):
                        continue
                    s.add((nx, ny))
                    q.append((nx, ny))
            if ok:
                break
        if ok:
            opened.add((gx, gy))
        else:
            err(f"L{level}: cannot push block to plate ({px},{py}) for gate ({gx},{gy})")
            bad = True

    seen = bfs(tiles, decor, start, opened, tele)
    for bx, by in buttons:
        adj = [(bx + dx, by + dy) for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))]
        if any(a in seen for a in adj):
            for g in gate_opens.get(events[by][bx], []):
                opened.add(g)
        else:
            err(f"L{level}: button ({bx},{by}) unreachable")
            bad = True

    seen = bfs(tiles, decor, start, opened, tele)
    for ax, ay in alcoves:
        adj = [(ax + dx, ay + dy) for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))]
        if any(a in seen for a in adj) and any(c in seen for c in chests):
            for g in gate_opens.get(events[ay][ax], []):
                opened.add(g)
        else:
            err(f"L{level}: alcove ({ax},{ay}) not completable")
            bad = True

    # gates without openers (except L10 quake)
    opener_gates = set()
    for gs in gate_opens.values():
        opener_gates.update(gs)
    for px, py, gx, gy in links:
        opener_gates.add((gx, gy))
    if level == 10:
        opener_gates.update(gates)
    for g in gates:
        if g not in opener_gates:
            err(f"L{level}: gate {g} has no opener")

    if level == 10:
        for g in gates:
            opened.add(g)

    # Signal Magic: Purge clears static piles (18), Decode opens locked doors (19)
    opened.update(find(decor, D_STATIC_PILE))
    opened.update(find(decor, D_LOCKED_DOOR))

    seen = bfs(tiles, decor, start, opened, tele)
    stairs_ok = any(s in seen for s in stairs) if stairs else (level == 10)
    boss_ok = all(b in seen for b in bosses) if bosses else True

    if not stairs_ok:
        err(f"L{level}: stairs unreachable after puzzles; stairs={stairs} opened={opened}")
    if not boss_ok:
        err(f"L{level}: boss unreachable; bosses={bosses}")

    status = "PASS" if stairs_ok and boss_ok and not bad and not any(
        e.startswith(f"L{level}:") for e in errors
    ) else "FAIL"
    # recount only this level's new errors for status — simpler: recompute
    return status, start


def audit_combat_sprites():
    combat = (SRC / "combat.c").read_text()
    slots = [int(m) for m in re.findall(r"\.sprite_slot\s*=\s*(\d+)", combat)]
    if len(slots) != ENEMY_COUNT:
        err(f"combat: expected {ENEMY_COUNT} sprite_slot entries, got {len(slots)}")
    for i, s in enumerate(slots):
        if s < 0 or s >= ENEMY_COUNT:  # merchant is slot 10, not an enemy
            err(f"combat: enemy {i} sprite_slot {s} OOB (need 0..{ENEMY_COUNT-1})")
    # unique preferred
    if len(set(slots)) < ENEMY_COUNT:
        warn(f"combat: sprite slots not unique: {slots}")

    monsters = SRC.parents[1] / "resources" / "images" / "monsters.png"
    try:
        from PIL import Image

        im = Image.open(monsters)
        expect_w = 58 * SPRITE_SLOTS
        if im.size != (expect_w, 63):
            err(f"monsters.png size {im.size}, expected ({expect_w}, 63)")
    except Exception as e:
        warn(f"could not check monsters.png: {e}")


def audit_l10_sequence():
    tiles, decor, events, start = parse_map(10)
    cmds, text = parse_events(10)
    # sequence path from gen: (6,9)(6,8)(5,8)(4,8)(4,9)
    path = [(6, 9), (6, 8), (5, 8), (4, 8), (4, 9)]
    for i, (x, y) in enumerate(path, 1):
        if events[y][x] < 0:
            err(f"L10: sequence tile {i} at ({x},{y}) has no event")
    if decor[8][14] != D_GATE:
        err(f"L10: boss gate missing at (14,8), decor={decor[8][14]}")
    if "THE CORE\nGATE OPENS" not in text and "THE CORE" not in text:
        # message may be split
        if "GATE OPENS" not in text:
            err("L10: missing quake gate-open message")
    if "ENEMY_LICH" not in text:
        err("L10: Architect battle missing")
    if "s_m10_end_dialog" not in text:
        err("L10: ending dialog missing")


def main() -> int:
    print("=== Legend of Pebble audit ===")
    audit_combat_sprites()
    starts = {}
    # pre-parse starts
    for level in range(1, 11):
        _, _, _, start = parse_map(level)
        starts[level] = start

    results = []
    for level in range(1, 11):
        before = len(errors)
        status, _ = audit_level(level, starts)
        # recompute status from new errors
        new_errs = [e for e in errors[before:] if e.startswith(f"L{level}:")]
        status = "PASS" if not new_errs else "FAIL"
        results.append((level, status))
        print(f"  L{level}: {status}  start={starts[level]}")

    audit_l10_sequence()

    print()
    if warnings:
        print(f"Warnings ({len(warnings)}):")
        for w in warnings:
            print(f"  ! {w}")
    if errors:
        print(f"Errors ({len(errors)}):")
        for e in errors:
            print(f"  X {e}")
        print("\nOVERALL: FAIL")
        return 1

    print("OVERALL: PASS")
    print(f"All levels: {results}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
