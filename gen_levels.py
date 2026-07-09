#!/usr/bin/env python3
"""Generate campaign levels 1..10 for Legend of Pebble (Pebble Time 2).

Emits, into LOP/src/c/:
  map1.h .. map10.h            (tiles / decor / event-id grids + start defines)
  map1events.inc .. map10events.inc  (event command tables)
  puzzle_links.inc             (per-map pressure-plate link tables)

All maps are 28x28. Levels chain N -> N+1 via stairs. Level 10 ends with a
dialog after the Architect is defeated.

ASCII legend (per cell):
  #  wall            %  illusory/secret wall (walkable, looks like wall)
  .  floor           ,  floor variant
  0/space void
  @  player start (floor)
  B  pushable block (floor)      p  pressure plate (floor)
  g  gate (floor + closed gate)  T  teleporter pad (floor)
  O  pit / trapdoor (floor)      c  chest -> key (floor)
  C  special chest (loot by level)  k  locked door (Decode)
  Z  static pile (Purge)         m  mimic chest ambush
  a  wall alcove (needs a key)   !  wall button (opens a gate)
  L  teleporter landing (floor)  <  stairs down (floor)
  X  boss battle tile (floor)    K  Keeper mid-boss
  S  skull / note (dialog)       H  haybed (rest)
  M  merchant (solid NPC, opens shop)
  1..5  earthquake sequence tiles (level 10 only)
"""

import os

SRC = os.path.join(os.path.dirname(__file__), "src", "c")

T_BLANK, T_FLOOR, T_FLOORR, T_WALL, T_PILLAR, T_SECRET = 0, 1, 2, 3, 5, 6
D_NONE, D_CHEST, D_HAYBED, D_SKULL, D_GATE = 0, 2, 3, 4, 6
D_MAREN = 9
D_BLOCK, D_PLATE_UP, D_TELEPORT, D_PIT, D_ALCOVE_EMPTY = 11, 12, 14, 15, 16
D_STATIC_PILE, D_LOCKED_DOOR = 18, 19

FACING_EAST = 1
DIM = 28

TILE_OF = {
    '#': T_WALL, '%': T_SECRET, '.': T_FLOOR, ',': T_FLOORR,
    '0': T_BLANK, ' ': T_BLANK, 'I': T_PILLAR,
    '@': T_FLOOR, 'B': T_FLOOR, 'p': T_FLOOR, 'g': T_FLOOR,
    'T': T_FLOOR, 'O': T_FLOOR, 'c': T_FLOOR, 'C': T_FLOOR, 'L': T_FLOOR,
    '<': T_FLOOR, 'X': T_FLOOR, 'K': T_FLOOR, 'S': T_FLOOR, 'H': T_FLOOR,
    'M': T_FLOOR, 'Z': T_FLOOR, 'm': T_FLOOR,
    '1': T_FLOOR, '2': T_FLOOR, '3': T_FLOOR, '4': T_FLOOR, '5': T_FLOOR,
    'a': T_WALL, '!': T_WALL, 'k': T_FLOOR,
}
DECOR_OF = {
    'B': D_BLOCK, 'p': D_PLATE_UP, 'g': D_GATE, 'T': D_TELEPORT,
    'O': D_PIT, 'c': D_CHEST, 'C': D_CHEST, 'm': D_CHEST,
    'a': D_ALCOVE_EMPTY, 'Z': D_STATIC_PILE, 'k': D_LOCKED_DOOR,
    'S': D_SKULL, 'H': D_HAYBED, 'M': D_MAREN,
}


def _mk(W, H):
    return [['#' if (x == 0 or x == W - 1 or y == 0 or y == H - 1) else '.'
             for x in range(W)] for y in range(H)]


def _carve_room(g, x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 < x < len(g[0]) - 1 and 0 < y < len(g) - 1:
                g[y][x] = '.'


def _wall_rect(g, x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < len(g[0]) and 0 <= y < len(g):
                g[y][x] = '#'


def _h_corridor(g, y, x0, x1):
    for x in range(min(x0, x1), max(x0, x1) + 1):
        if 0 < y < len(g) - 1 and 0 < x < len(g[0]) - 1:
            g[y][x] = '.'


def _v_corridor(g, x, y0, y1):
    for y in range(min(y0, y1), max(y0, y1) + 1):
        if 0 < x < len(g[0]) - 1 and 0 < y < len(g) - 1:
            g[y][x] = '.'


def _exit_chamber(g, gate=True, gx=None):
    H = len(g)
    W = len(g[0])
    wy = H - 3
    for x in range(1, W - 1):
        g[wy][x] = '#'
    if gx is None:
        gx = W - 3
    g[wy][gx] = 'g' if gate else '.'
    g[H - 2][gx] = '<'
    return gx, wy


def _pillars(g, cells):
    for (x, y) in cells:
        if 0 < y < len(g) - 1 and 0 < x < len(g[0]) - 1 and g[y][x] == '.':
            g[y][x] = 'I'


def _rows(g):
    return [''.join(r) for r in g]


# ---- Distinct LOP layouts ------------------------------------------------

def _lvl1():
    # Cell wake-up: short L-corridor, haybed, one switch gate, stairs.
    g = _mk(16, 12)
    g[2][2] = '@'
    g[2][3] = 'H'
    g[2][8] = 'S'                          # note about the Rift
    g[3][6] = 'C'                          # Stick chest
    for x in range(2, 12):
        g[5][x] = '#'
    g[5][4] = 'g'                          # gate mid-corridor
    g[1][10] = '!'                         # button opens the gate
    _exit_chamber(g, gate=False, gx=12)
    _pillars(g, [(6, 3), (10, 7)])
    return dict(level=1, rows=_rows(g), theme="cell")


def _lvl2():
    # Twin halls split by a wall; teleporter crosses; chest+alcove opens exit.
    g = _mk(20, 14)
    g[1][1] = '@'
    for y in range(1, 11):
        g[y][9] = '#'
    g[3][3] = 'T'
    g[3][14] = 'L'
    g[8][2] = 'c'
    g[8][16] = 'S'                         # Maren scrap note
    g[2][6] = 'M'                          # merchant
    gx, wy = _exit_chamber(g, gate=True, gx=16)
    g[wy][gx - 3] = 'a'
    _pillars(g, [(4, 6), (14, 6), (6, 10)])
    return dict(level=2, rows=_rows(g), theme="rift")


def _lvl3():
    # Cross-shaped hall; push block onto plate for exit.
    g = _mk(18, 14)
    for y in range(1, 13):
        for x in range(1, 17):
            g[y][x] = '#'
    _carve_room(g, 7, 1, 10, 12)
    _carve_room(g, 1, 5, 16, 8)
    g[2][8] = '@'
    g[6][3] = 'B'
    g[6][14] = 'p'
    g[3][9] = 'C'                          # Spellbook: Heal
    for x in range(1, 17):
        g[11][x] = '#'
    g[11][8] = 'g'
    g[12][8] = '<'
    for y in (9, 10):
        g[y][8] = '.'
        g[y][9] = '.'
    _pillars(g, [(8, 4), (9, 10)])
    return dict(level=3, rows=_rows(g), theme="cross")


def _lvl4():
    # Ring corridor around a solid core; teleporter drops you inside, walk south out.
    g = _mk(20, 15)
    for y in range(4, 11):
        for x in range(5, 14):
            g[y][x] = '#'
    for y in range(5, 10):
        for x in range(6, 13):
            g[y][x] = '.'
    g[1][1] = '@'
    g[2][2] = 'T'
    g[7][9] = 'L'
    g[10][9] = '.'
    g[11][9] = '.'
    g[6][7] = 'C'                          # +5 Max HP gem
    for (x, y) in [(3, 3), (16, 3), (3, 12), (16, 12)]:
        g[y][x] = 'O'
    _exit_chamber(g, gate=False, gx=9)
    return dict(level=4, rows=_rows(g), theme="ring")


def _lvl5():
    # Zigzag pit gauntlet — safe path snakes through. Keeper waits near exit.
    g = _mk(22, 14)
    g[1][1] = '@'
    for y in range(3, 11):
        for x in range(2, 20):
            g[y][x] = 'O'
    path = [(2, 3), (3, 3), (4, 3), (4, 4), (4, 5), (5, 5), (6, 5), (7, 5),
            (7, 6), (7, 7), (8, 7), (9, 7), (10, 7), (10, 8), (10, 9),
            (11, 9), (12, 9), (13, 9), (14, 9), (14, 10), (15, 10),
            (16, 10), (17, 10), (18, 10), (19, 10)]
    for (x, y) in path:
        g[y][x] = '.'
    g[2][18] = 'S'
    g[1][3] = 'M'
    g[10][17] = 'K'                        # The Keeper mid-boss
    _exit_chamber(g, gate=False, gx=19)
    return dict(level=5, rows=_rows(g), theme="zigzag")


def _lvl6():
    # Three chambers. Button + chest in chamber 1 open gate 1.
    g = _mk(24, 14)
    for y in range(1, 13):
        g[y][7] = '#'
        g[y][15] = '#'
    g[1][1] = '@'
    g[4][7] = 'g'
    g[4][15] = 'g'
    g[2][3] = 'c'
    g[2][5] = '!'
    g[6][15] = 'a'
    g[10][18] = 'H'
    g[8][10] = 'C'                         # +1 Atk gem
    g[9][11] = 'Z'                         # static pile (needs Purge later)
    _exit_chamber(g, gate=False, gx=20)
    _pillars(g, [(3, 6), (11, 6), (19, 6)])
    return dict(level=6, rows=_rows(g), theme="chambers")


def _lvl7():
    # Illusory wall maze + static piles + locked door shortcut.
    g = _mk(22, 16)
    g[1][1] = '@'
    for x in range(1, 21):
        g[4][x] = '#'
        g[8][x] = '#'
        g[12][x] = '#'
    g[4][3] = '%'
    g[4][14] = '.'
    g[8][8] = '%'
    g[8][18] = '.'
    g[12][5] = '.'
    g[12][16] = '%'
    g[12][18] = '.'
    for x in range(1, 21):
        g[13][x] = '#'
    g[13][16] = '!'
    g[13][18] = 'g'
    g[14][17] = '.'
    g[14][18] = '<'
    g[14][19] = '.'
    g[6][10] = 'S'
    g[2][2] = 'M'
    g[2][5] = 'C'                          # Spellbook: Purge
    g[6][3] = 'Z'
    g[10][14] = 'Z'
    g[6][16] = 'k'                         # locked door shortcut
    _pillars(g, [(6, 2), (16, 6), (10, 10)])
    return dict(level=7, rows=_rows(g), theme="illusion")


def _lvl8():
    # Block+plate + teleporter shortcut over a pit field.
    g = _mk(24, 16)
    g[1][1] = '@'
    g[2][3] = 'B'
    g[2][12] = 'p'
    g[5][2] = 'T'
    g[5][20] = 'L'
    for y in range(7, 12):
        for x in range(4, 18):
            g[y][x] = 'O'
    for x in range(4, 18):
        g[9][x] = '.'
    g[9][10] = 'S'
    g[3][6] = 'm'                          # mimic ambush
    g[3][8] = 'C'                          # +1 Def gem
    g[11][20] = 'Z'
    g[12][19] = 'k'
    _exit_chamber(g, gate=True, gx=20)
    _pillars(g, [(6, 3), (18, 3), (6, 13), (18, 13)])
    return dict(level=8, rows=_rows(g), theme="bridge")


def _lvl9():
    # Dense pit lattice + block/plate; secret wall drops onto the safe path.
    g = _mk(24, 17)
    g[1][1] = '@'
    g[2][2] = 'B'
    g[2][14] = 'p'
    for y in range(4, 14):
        for x in range(2, 22):
            if (x + y) % 2 == 0:
                g[y][x] = 'O'
    path = [(3, 4), (3, 5), (3, 6), (4, 6), (5, 6), (6, 6), (7, 6),
            (7, 7), (7, 8), (8, 8), (9, 8), (10, 8), (11, 8),
            (11, 9), (11, 10), (12, 10), (13, 10), (14, 10), (15, 10),
            (15, 11), (15, 12), (16, 12), (17, 12), (18, 12), (19, 12),
            (19, 13), (20, 13)]
    for (x, y) in path:
        g[y][x] = '.'
    for x in range(1, 23):
        g[3][x] = '#'
    g[3][3] = '%'
    for x in range(1, 23):
        g[14][x] = '#'
    g[14][20] = 'g'
    g[15][18] = '.'
    g[15][19] = '.'
    g[15][20] = '<'
    g[15][21] = '.'
    g[8][9] = 'Z'
    g[12][16] = 'Z'
    g[10][12] = 'C'                        # gold cache
    g[6][7] = 'm'                          # mimic
    return dict(level=9, rows=_rows(g), theme="lattice")


def _lvl10():
    # Finale arena. Sequence path: NORTH NORTH WEST WEST SOUTH opens boss gate.
    g = _mk(26, 18)
    g[1][1] = '@'
    g[2][2] = 'H'
    g[2][4] = 'S'
    g[1][8] = 'M'

    for (x, y) in [(6, 11), (6, 10), (6, 9), (6, 8), (5, 8), (4, 8), (4, 9),
                   (4, 10), (5, 10), (7, 10), (7, 9), (7, 8)]:
        g[y][x] = '.'
    g[9][6] = '1'
    g[8][6] = '2'
    g[8][5] = '3'
    g[8][4] = '4'
    g[9][4] = '5'

    # Wall off the boss arena on the east; gate opens via earthquake
    for y in range(1, 15):
        g[y][14] = '#'
    g[8][14] = 'g'                         # boss gate (opened by sequence)
    for y in range(4, 13):
        for x in range(15, 24):
            g[y][x] = '.'
    g[8][18] = 'X'                         # Architect
    for (x, y) in [(16, 5), (22, 5), (16, 11), (22, 11)]:
        g[y][x] = 'O'
    _pillars(g, [(17, 7), (20, 7), (17, 9), (20, 9)])

    # Victory stairs behind the boss (open doorway, no gate — boss fight is the lock)
    for x in range(1, 25):
        g[15][x] = '#'
    g[15][18] = '.'
    g[16][18] = '<'
    return dict(level=10, rows=_rows(g), theme="core")


LEVELS = [_lvl1(), _lvl2(), _lvl3(), _lvl4(), _lvl5(),
          _lvl6(), _lvl7(), _lvl8(), _lvl9(), _lvl10()]


def blank_grid(fill):
    return [[fill for _ in range(DIM)] for _ in range(DIM)]


def emit_grid(name, ctype, grid):
    lines = [f"static const {ctype} {name}[{DIM}][{DIM}] = {{"]
    for row in grid:
        lines.append("    { " + ",".join(f"{v:>2}" for v in row) + " },")
    lines.append("};")
    return "\n".join(lines)


flag_counter = [1]  # reserve 0; each map gets unique flags


def next_flag():
    f = flag_counter[0]
    flag_counter[0] += 1
    return f


plate_tables = {}


# Per-level story dialogs (title, pages)
STORY = {
    1: ("PULLED\nTHROUGH", [
        "YOU CHECK\nTHE TIME.",
        "THE WATCH\nFLARES.\nTHE WORLD\nTWISTS.",
        "YOU WAKE\nIN A CELL.\nNOT YOUR\nWORLD.",
        "FIND A WAY\nHOME!",
    ]),
    2: ("THE RIFT", [
        "THE AIR\nHUMS WITH\nSTATIC.",
        "OTHER\nWATCHES\nWERE PULLED\nHERE TOO.",
        "SOMEONE\nLEFT A\nNOTE...",
    ]),
}


def dialog_c_block(sym, title, pages):
    out = []
    out.append(f"static const DialogPage {sym}_pages[] = {{")
    for p in pages:
        # Python strings may contain real newlines; emit C \\n escapes.
        esc = (p.replace("\\", "\\\\")
                .replace("\n", "\\n")
                .replace('"', '\\"'))
        out.append(f'    {{ "{esc}" }},')
    out.append("};")
    title_esc = title.replace("\n", "\\n")
    out.append(f"static const DialogDef {sym}_dialog = {{")
    out.append(f'    .title = "{title_esc}",')
    out.append(f"    .pages = {sym}_pages,")
    out.append(f"    .page_count = ARRAY_LENGTH({sym}_pages),")
    out.append("};")
    out.append("")
    return out


def build_level(lvl):
    level = lvl["level"]
    mapid = level - 1
    rows = lvl["rows"]
    W = len(rows[0])
    for r in rows:
        assert len(r) == W, f"L{level}: row width {len(r)} != {W}: {r!r}"
    assert W <= DIM and len(rows) <= DIM

    tiles = blank_grid(T_BLANK)
    decor = blank_grid(D_NONE)
    events = blank_grid(-1)

    start = None
    plates, gates, teleporters, pits, chests, alcoves, buttons = \
        [], [], [], [], [], [], []
    stairs, boss, skulls, haybeds, merchants = [], [], [], [], []
    special_chests, keepers, statics, locked, mimics = [], [], [], [], []
    seq = {}  # digit -> (x,y)
    landing = None

    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            tiles[y][x] = TILE_OF[ch]
            if ch in DECOR_OF:
                decor[y][x] = DECOR_OF[ch]
            if ch == '@':
                start = (x, y)
            elif ch == 'p':
                plates.append((x, y))
            elif ch == 'g':
                gates.append((x, y))
            elif ch == 'T':
                teleporters.append((x, y))
            elif ch == 'O':
                pits.append((x, y))
            elif ch == 'c':
                chests.append((x, y))
            elif ch == 'C':
                special_chests.append((x, y))
            elif ch == 'm':
                mimics.append((x, y))
            elif ch == 'a':
                alcoves.append((x, y))
            elif ch == '!':
                buttons.append((x, y))
            elif ch == '<':
                stairs.append((x, y))
            elif ch == 'X':
                boss.append((x, y))
            elif ch == 'K':
                keepers.append((x, y))
            elif ch == 'Z':
                statics.append((x, y))
            elif ch == 'k':
                locked.append((x, y))
            elif ch == 'L':
                landing = (x, y)
            elif ch == 'S':
                skulls.append((x, y))
            elif ch == 'H':
                haybeds.append((x, y))
            elif ch == 'M':
                merchants.append((x, y))
            elif ch in '12345':
                seq[int(ch)] = (x, y)

    assert start, f"L{level}: no start"

    links = []
    for i, (px, py) in enumerate(plates):
        assert i < len(gates), f"L{level}: plate without gate"
        gx, gy = gates[i]
        links.append((px, py, gx, gy))
    free_gates = gates[len(plates):]
    plate_tables[mapid] = links

    fgi = [0]

    def take_free_gate():
        assert fgi[0] < len(free_gates), f"L{level}: no free gate left"
        g = free_gates[fgi[0]]
        fgi[0] += 1
        return g

    evs = []
    dialogs = []  # extra C dialog defs to prepend

    def add_event(cmds, trigger):
        evs.append((cmds, trigger))
        return len(evs) - 1

    # id 0 = START
    if level in STORY:
        title, pages = STORY[level]
        dialogs.extend(dialog_c_block(f"s_m{level}_intro", title, pages))
        add_event([
            f'{{ .type=CMD_DIALOG, .dialog={{ &s_m{level}_intro_dialog }} }}',
        ], "NO_EVENT_TRIGGER")
    else:
        add_event([f'{{ .type=CMD_DISPLAY_MESSAGE, .message={{ "LEVEL {level}", 25 }} }}'],
                  "NO_EVENT_TRIGGER")

    sx, sy = start

    # stairs — land on the next map's authored start, not a hardcoded (1,1)
    for (x, y) in stairs:
        if level < 10:
            nxt = level  # next mapid (0-based: level N stairs → mapid N)
            nsx, nsy = None, None
            for ny, nrow in enumerate(LEVELS[level]["rows"]):
                for nx, nch in enumerate(nrow):
                    if nch == '@':
                        nsx, nsy = nx, ny
                        break
                if nsx is not None:
                    break
            assert nsx is not None, f"L{level}: next map has no start"
            cmds = [
                '{ .type=CMD_DISPLAY_MESSAGE, .message={ "DESCENDING...", 20 } }',
                f'{{ .type=CMD_CHANGE_MAP, .change_map={{ {nxt}, {nsx}, {nsy}, {FACING_EAST} }} }}',
            ]
        else:
            cmds = ['{ .type=CMD_DIALOG, .dialog={ &s_m10_end_dialog } }']
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # teleporters
    if teleporters:
        assert landing, f"L{level}: teleporter without landing"
        lx, ly = landing
        for (x, y) in teleporters:
            cmds = [
                '{ .type=CMD_DISPLAY_MESSAGE, .message={ "BLINK!", 15 } }',
                f'{{ .type=CMD_MOVE_PLAYER, .move_player={{ {lx}, {ly}, {FACING_EAST} }} }}',
            ]
            events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # pits
    if pits:
        pit_dmg = max(2, level)
        cmds = [
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "YOU FALL!", 20 } }',
            '{ .type=CMD_SHAKE, .shake={ 8, 4 } }',
            f'{{ .type=CMD_DAMAGE, .damage={{ {pit_dmg} }} }}',
            f'{{ .type=CMD_MOVE_PLAYER, .move_player={{ {sx}, {sy}, {FACING_EAST} }} }}',
        ]
        pit_ev = add_event(cmds, "EVENT_TRIGGER_STEP")
        for (x, y) in pits:
            events[y][x] = pit_ev

    # chests (keys)
    for (x, y) in chests:
        f = next_flag()
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            '{ .type=CMD_GIVE_ITEM, .give_item={ 1, 1 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 11, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "FOUND A KEY", 25 } }',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {x}, {y}, DECOR_NONE }} }}',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # Special loot chests by floor (Stick / Heal / gems / gold)
    # kind: 0=max_hp 1=max_mp 2=atk 3=def
    SPECIAL_LOOT = {
        1: ("STICK!", [
            '{ .type=CMD_SET_WEAPON, .set_weapon={ 1 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 14, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "FOUND A STICK", 25 } }',
        ]),
        3: ("HEAL!", [
            '{ .type=CMD_SET_SPELLBOOK, .set_spellbook={ 1 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 12, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "SPELLBOOK:\\nHEAL", 30 } }',
        ]),
        4: ("HP UP!", [
            '{ .type=CMD_GRANT_BONUS, .grant_bonus={ 0, 5 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 13, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "MAX HP +5", 25 } }',
        ]),
        6: ("ATK UP!", [
            '{ .type=CMD_GRANT_BONUS, .grant_bonus={ 2, 1 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 14, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "ATK +1", 25 } }',
        ]),
        7: ("PURGE!", [
            '{ .type=CMD_SET_SPELLBOOK, .set_spellbook={ 2 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 12, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "SPELLBOOK:\\nPURGE", 30 } }',
        ]),
        8: ("DEF UP!", [
            '{ .type=CMD_GRANT_BONUS, .grant_bonus={ 3, 1 } }',
            '{ .type=CMD_DISPLAY_ICON, .display_icon={ 15, 25 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "DEF +1", 25 } }',
        ]),
        9: ("GOLD!", [
            '{ .type=CMD_ADD_GOLD, .add_gold={ 50 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "GOLD CACHE\\n+50", 25 } }',
        ]),
    }
    for (x, y) in special_chests:
        f = next_flag()
        loot = SPECIAL_LOOT.get(level)
        if not loot:
            loot = ("LOOT!", [
                '{ .type=CMD_GIVE_ITEM, .give_item={ 0, 1 } }',
                '{ .type=CMD_DISPLAY_MESSAGE, .message={ "FOUND LOOT", 25 } }',
            ])
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
        ] + list(loot[1]) + [
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {x}, {y}, DECOR_NONE }} }}',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # Mimic chests — ambush then clear
    for (x, y) in mimics:
        f = next_flag()
        victory = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {x}, {y}, DECOR_NONE }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "MIMIC DOWN!", 25 } }',
        ], "NO_EVENT_TRIGGER")
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "IT BITES!", 18 } }',
            f'{{ .type=CMD_BATTLE, .battle={{ ENEMY_GOLEM, {victory}, true }} }}',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # Static piles — blocked until Purge (explore magic clears decor; no event needed)
    # Locked doors — blocked until Decode (same)
    _ = statics, locked  # placed as decor only

    # The Keeper mid-boss
    for (x, y) in keepers:
        f = next_flag()
        victory = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            r'{ .type=CMD_DISPLAY_MESSAGE, .message={ "THE KEEPER\nFALLS...", 30 } }',
        ], "NO_EVENT_TRIGGER")
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            r'{ .type=CMD_DISPLAY_MESSAGE, .message={ "THE KEEPER\nBLOCKS THE\nPATH!", 25 } }',
            f'{{ .type=CMD_BATTLE, .battle={{ ENEMY_WARDEN, {victory}, false }} }}',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # buttons first so early-chamber buttons claim the first free gates
    for (x, y) in buttons:
        gx, gy = take_free_gate()
        f = next_flag()
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            '{ .type=CMD_SHAKE, .shake={ 6, 3 } }',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {gx}, {gy}, DECOR_NONE }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "A GATE OPENS", 25 } }',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_FORWARD")

    # alcoves (consume a key to open a later free gate)
    for (x, y) in alcoves:
        gx, gy = take_free_gate()
        fail = add_event(
            ['{ .type=CMD_DISPLAY_MESSAGE, .message={ "NEEDS A KEY", 20 } }'],
            "NO_EVENT_TRIGGER")
        f = next_flag()
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            f'{{ .type=CMD_ITEM_CHECK, .item_check={{ 1, {fail} }} }}',
            '{ .type=CMD_TAKE_ITEM, .take_item={ 1, 1 } }',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {x}, {y}, DECOR_ALCOVE_FULL }} }}',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {gx}, {gy}, DECOR_NONE }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "THE GATE OPENS", 25 } }',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_FORWARD")

    # haybeds
    for (x, y) in haybeds:
        cmds = [
            '{ .type=CMD_SET_RESPAWN }',
            '{ .type=CMD_HEAL_HP, .heal_hp={ -1 } }',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "YOU RESTED.", 25 } }',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    # skulls / notes
    for i, (x, y) in enumerate(skulls):
        if level == 10 and i == 0:
            title = "A NOTE"
            pages = [
                "SCRATCHED\nON THE\nWALL:",
                "WALK THE\nPATH:",
                "NORTH.\nNORTH.\nWEST.\nWEST.\nSOUTH.",
                "THE CORE\nWILL\nSHAKE.",
                "THEN THE\nGATE\nOPENS.",
            ]
        elif level == 1:
            title = "OLD BONES"
            pages = [
                "SCRATCHED\nIN STONE:",
                "THE WATCH\nPULLED\nANOTHER.",
                "THE SIGNAL\nNEVER\nSTOPS.",
            ]
        elif level == 2:
            title = "A NOTE"
            pages = [
                "I'M MAREN.",
                "I WAS A\nCODER.\nLIKE YOU.",
                "THE SIGNAL\nKEEPERS\nRULE THE\nRIFT.",
                "FIND THE\nCORE.\nBREAK THE\nSIGNAL.",
            ]
        else:
            title = "A NOTE"
            pages = [
                "THE RIFT\nRUNS\nDEEPER.",
                "STAY\nSHARP.",
            ]
        sym = f"s_m{level}_note{i}"
        dialogs.extend(dialog_c_block(sym, title, pages))
        events[y][x] = add_event(
            [f'{{ .type=CMD_DIALOG, .dialog={{ &{sym}_dialog }} }}'],
            "EVENT_TRIGGER_FORWARD")

    # merchants — walk into them to open the shop (with floor gossip via shop UI)
    for (x, y) in merchants:
        events[y][x] = add_event([
            '{ .type=CMD_OPEN_SHOP }',
        ], "EVENT_TRIGGER_FORWARD")

    # Level 10: NORTH NORTH WEST WEST SOUTH step sequence -> earthquake
    # Absolute map directions (not button labels). Note text matches.
    # FLAG_CHECK jumps when the flag IS set. So "require prev step" is:
    #   FLAG_CHECK prev, ok_ev ; JUMP reset_ev
    if level == 10 and seq:
        assert set(seq.keys()) == {1, 2, 3, 4, 5}, f"L10 seq tiles: {seq.keys()}"
        assert free_gates, "L10 needs a free gate for the sequence"
        gx, gy = take_free_gate()
        f_done = next_flag()
        f1, f2, f3, f4 = next_flag(), next_flag(), next_flag(), next_flag()

        reset_ev = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f1}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f2}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f3}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f4}, false }} }}',
            r'{ .type=CMD_DISPLAY_MESSAGE, .message={ "SEQUENCE\nRESET.", 20 } }',
        ], "NO_EVENT_TRIGGER")

        ok_ev = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f_done}, true }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f1}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f2}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f3}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f4}, false }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "EARTHQUAKE!", 30 } }',
            '{ .type=CMD_SHAKE, .shake={ 16, 5 } }',
            f'{{ .type=CMD_CHANGE_DECOR, .change_decor={{ {gx}, {gy}, DECOR_NONE }} }}',
            r'{ .type=CMD_DISPLAY_MESSAGE, .message={ "THE CORE\nGATE OPENS!", 30 } }',
        ], "NO_EVENT_TRIGGER")

        # Advance helpers: set next flag + flash step number
        adv2 = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f2}, true }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "2...", 12 } }',
        ], "NO_EVENT_TRIGGER")
        adv3 = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f3}, true }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "3...", 12 } }',
        ], "NO_EVENT_TRIGGER")
        adv4 = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f4}, true }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "4...", 12 } }',
        ], "NO_EVENT_TRIGGER")

        # Tile 1: begin sequence
        x, y = seq[1]
        events[y][x] = add_event([
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f_done}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f1}, true }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f2}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f3}, false }} }}',
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f4}, false }} }}',
            '{ .type=CMD_DISPLAY_MESSAGE, .message={ "1...", 12 } }',
        ], "EVENT_TRIGGER_STEP")

        # Tile 2: require f1
        x, y = seq[2]
        events[y][x] = add_event([
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f_done}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f1}, {adv2} }} }}',
            f'{{ .type=CMD_JUMP, .jump={{ {reset_ev} }} }}',
        ], "EVENT_TRIGGER_STEP")

        # Tile 3: require f2
        x, y = seq[3]
        events[y][x] = add_event([
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f_done}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f2}, {adv3} }} }}',
            f'{{ .type=CMD_JUMP, .jump={{ {reset_ev} }} }}',
        ], "EVENT_TRIGGER_STEP")

        # Tile 4: require f3
        x, y = seq[4]
        events[y][x] = add_event([
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f_done}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f3}, {adv4} }} }}',
            f'{{ .type=CMD_JUMP, .jump={{ {reset_ev} }} }}',
        ], "EVENT_TRIGGER_STEP")

        # Tile 5: require f4 -> earthquake
        x, y = seq[5]
        events[y][x] = add_event([
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f_done}, -1 }} }}',
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f4}, {ok_ev} }} }}',
            f'{{ .type=CMD_JUMP, .jump={{ {reset_ev} }} }}',
        ], "EVENT_TRIGGER_STEP")

    # boss (Architect on L10; none earlier unless marked)
    for (x, y) in boss:
        f = next_flag()
        # After victory, just message — stairs are already open behind
        victory = add_event([
            f'{{ .type=CMD_FLAG_CHANGE, .flag_change={{ {f}, true }} }}',
            r'{ .type=CMD_DISPLAY_MESSAGE, .message={ "THE SIGNAL\nBREAKS...", 30 } }',
        ], "NO_EVENT_TRIGGER")
        cmds = [
            f'{{ .type=CMD_FLAG_CHECK, .flag_check={{ {f}, -1 }} }}',
            f'{{ .type=CMD_BATTLE, .battle={{ ENEMY_LICH, {victory}, false }} }}',
        ]
        events[y][x] = add_event(cmds, "EVENT_TRIGGER_STEP")

    return dict(level=level, mapid=mapid, tiles=tiles, decor=decor,
                events=events, start=start, evs=evs, dialogs=dialogs)


def write_map_header(b):
    level = b["level"]
    p = os.path.join(SRC, f"map{level}.h")
    sx, sy = b["start"]
    out = [
        "#pragma once",
        f"// AUTO-GENERATED by gen_levels.py — Legend of Pebble level {level}.",
        f"#define MAP{level}_W {DIM}",
        f"#define MAP{level}_H {DIM}",
        "",
        emit_grid(f"MAP{level}_TILES", "uint8_t", b["tiles"]),
        "",
        emit_grid(f"MAP{level}_DECOR", "uint8_t", b["decor"]),
        "",
        emit_grid(f"MAP{level}_EVENTS", "int8_t", b["events"]),
        "",
        f"#define MAP{level}_START_X {sx}",
        f"#define MAP{level}_START_Y {sy}",
        f"#define MAP{level}_START_FACING FACING_EAST",
        "",
    ]
    with open(p, "w") as fh:
        fh.write("\n".join(out))


def write_events_inc(b):
    level = b["level"]
    p = os.path.join(SRC, f"map{level}events.inc")
    out = [
        '#include "pebble.h"',
        '#include "event.h"',
        '#include "map.h"',
        '#include "player.h"',
        '#include "combat.h"',
        f"// AUTO-GENERATED by gen_levels.py — Legend of Pebble level {level}.",
        "",
    ]
    if level == 10:
        out.extend(dialog_c_block("s_m10_end", "HOME", [
            "THE WATCH\nFLARES\nAGAIN.",
            "TEN LEVELS.\nTHE SIGNAL\nBREAKS.",
            "YOU WAKE\nAT HOME.\nTHANKS FOR\nPLAYING!",
        ]))
    out.extend(b["dialogs"])

    for i, (cmds, _trig) in enumerate(b["evs"]):
        out.append(f"static const EventCmd s_m{level}_ev{i}_cmds[] = {{")
        for c in cmds:
            out.append(f"    {c},")
        out.append("};")
    out.append("")
    out.append(f"const Event MAP{level}_EVENT_TABLE[] = {{")
    for i, (_cmds, trig) in enumerate(b["evs"]):
        out.append(
            f"    {{ s_m{level}_ev{i}_cmds, "
            f"ARRAY_LENGTH(s_m{level}_ev{i}_cmds), {trig} }},")
    out.append("};")
    out.append(f"#define MAP{level}_EVENT_COUNT "
               f"ARRAY_LENGTH(MAP{level}_EVENT_TABLE)")
    out.append(f"#define MAP{level}_START_EVENT 0")
    out.append("")
    with open(p, "w") as fh:
        fh.write("\n".join(out))


def write_puzzle_links(map_count):
    p = os.path.join(SRC, "puzzle_links.inc")
    out = [
        "// AUTO-GENERATED by gen_levels.py — pressure-plate link tables.",
        "",
    ]
    for mapid in sorted(plate_tables):
        links = plate_tables[mapid]
        if not links:
            continue
        out.append(f"static const PlateLink s_map{mapid}_plates[] = {{")
        for (px, py, gx, gy) in links:
            out.append(
                f"    {{ {px}, {py}, {gx}, {gy}, DECOR_NONE, DECOR_GATE, "
                "PUZZLE_KEEP, PUZZLE_KEEP, -1 },")
        out.append("};")
    out.append("")
    out.append("static const PlateLink *const PUZZLE_TABLES[] = {")
    for mapid in range(map_count):
        out.append(f"    s_map{mapid}_plates," if plate_tables.get(mapid)
                   else "    NULL,")
    out.append("};")
    out.append("static const int PUZZLE_TABLE_COUNTS[] = {")
    for mapid in range(map_count):
        out.append(f"    {len(plate_tables.get(mapid, []))},")
    out.append("};")
    out.append("#define PUZZLE_MAP_COUNT "
               "((int)(sizeof(PUZZLE_TABLES)/sizeof(PUZZLE_TABLES[0])))")
    out.append("")
    with open(p, "w") as fh:
        fh.write("\n".join(out))


def main():
    built = [build_level(l) for l in LEVELS]
    for b in built:
        write_map_header(b)
        write_events_inc(b)
    for mid in range(10):
        plate_tables.setdefault(mid, [])
    write_puzzle_links(10)
    print("Generated Legend of Pebble levels 1-10 into", SRC)
    for b in built:
        print(f"  L{b['level']} (mapid {b['mapid']}): "
              f"{len(b['evs'])} events, {len(plate_tables[b['mapid']])} plates, "
              f"start {b['start']}")


if __name__ == "__main__":
    main()
