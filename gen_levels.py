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
import random
from collections import deque

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


# ---- Maze machinery (levels 3+) -------------------------------------------
# Seeded recursive-backtracker mazes: 1-wide corridors, optional loops.
# Cells live on odd coordinates; walls between. Deterministic per seed.

def _maze(W, H, seed, loops=0):
    rnd = random.Random(seed)
    g = [['#'] * W for _ in range(H)]
    stack = [(1, 1)]
    g[1][1] = '.'
    while stack:
        x, y = stack[-1]
        dirs = [(2, 0), (-2, 0), (0, 2), (0, -2)]
        rnd.shuffle(dirs)
        for dx, dy in dirs:
            nx, ny = x + dx, y + dy
            if 1 <= nx < W - 1 and 1 <= ny < H - 1 and g[ny][nx] == '#':
                g[y + dy // 2][x + dx // 2] = '.'
                g[ny][nx] = '.'
                stack.append((nx, ny))
                break
        else:
            stack.pop()
    # Knock a few walls to add loops (keeps navigation confusing, less linear)
    cand = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1)
            if g[y][x] == '#'
            and ((g[y][x - 1] == '.' and g[y][x + 1] == '.')
                 or (g[y - 1][x] == '.' and g[y + 1][x] == '.'))]
    rnd.shuffle(cand)
    for (x, y) in cand[:loops]:
        g[y][x] = '.'
    return g


_SOLID = set('#0 IgBZkOM')


def _reach_ascii(g, start):
    """BFS over the ASCII grid; gates/blocks/statics/locked/merchant solid."""
    H, W = len(g), len(g[0])
    q = deque([start])
    seen = {start}
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (0 <= nx < W and 0 <= ny < H and (nx, ny) not in seen
                    and g[ny][nx] not in _SOLID):
                seen.add((nx, ny))
                q.append((nx, ny))
    return seen


def _far_dead_ends(g, start):
    """Dead-end cells sorted farthest-first by BFS distance from start."""
    H, W = len(g), len(g[0])
    dist = {start: 0}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (0 <= nx < W and 0 <= ny < H and (nx, ny) not in dist
                    and g[ny][nx] not in _SOLID):
                dist[(nx, ny)] = dist[(x, y)] + 1
                q.append((nx, ny))
    ends = []
    for (x, y), d in dist.items():
        if (x, y) == start or g[y][x] != '.':
            continue
        walls = sum(1 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                    if g[y + dy][x + dx] == '#')
        if walls == 3:
            ends.append((d, x, y))
    ends.sort(reverse=True)
    return [(x, y) for (_d, x, y) in ends]


def _dead_end_neighbor(g, x, y):
    """The single corridor cell adjacent to a dead end (its neck)."""
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if g[y + dy][x + dx] not in _SOLID and g[y + dy][x + dx] != '#':
            return (x + dx, y + dy)
    return None


def _exit_columns(g):
    """Columns whose bottom maze row (H-2) is corridor — valid exit hookups."""
    H, W = len(g), len(g[0])
    return [x for x in range(1, W - 1) if g[H - 2][x] == '.']


def _append_plain_exit(g, gx, door='.'):
    """Open/gated/Keeper doorway in the bottom border + stairs row below."""
    W = len(g[0])
    g[-1] = ['#'] * W
    g[-1][gx] = door                     # '.', 'g' or 'K'
    stairs = ['#'] * W
    stairs[gx] = '<'
    g.append(stairs)


def _append_block_exit(g, door_x):
    """Grimrock lock: a 1-tall antechamber with a pushable block. Shove the
    block east onto the plate to hold the stair gate (just west of the block)
    open. The plate sits against the corridor's east wall so the block can
    never overshoot, and the corridor is 1 tile tall so it can never be
    wedged sideways."""
    W = len(g[0])
    px = W - 3                           # plate, wall right behind it
    bx = door_x + 2                      # block — player pushes from the west
    gx = door_x + 1                      # stair gate, west of the block
    assert bx < px
    g[-1] = ['#'] * W
    g[-1][door_x] = '.'                  # doorway from the maze
    corridor = ['#'] * W
    for x in range(door_x, px + 1):
        corridor[x] = '.'
    corridor[bx] = 'B'
    corridor[px] = 'p'
    g.append(corridor)
    gate_row = ['#'] * W
    gate_row[gx] = 'g'
    g.append(gate_row)
    stairs = ['#'] * W
    stairs[gx] = '<'
    g.append(stairs)


def _sprinkle_pits(g, start, tips, count):
    """Turn dead-end tips into pits (fall damage + reset) — never on the path."""
    placed = 0
    for (x, y) in tips:
        if placed >= count:
            break
        if g[y][x] == '.':
            g[y][x] = 'O'
            placed += 1


def _secret_shortcuts(g, seed, count):
    """Convert a few corridor-separating walls into illusory walls."""
    rnd = random.Random(seed)
    H, W = len(g), len(g[0])
    cand = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1)
            if g[y][x] == '#'
            and ((g[y][x - 1] == '.' and g[y][x + 1] == '.')
                 or (g[y - 1][x] == '.' and g[y + 1][x] == '.'))]
    rnd.shuffle(cand)
    for (x, y) in cand[:count]:
        g[y][x] = '%'


def _locked_shortcut(g, seed):
    """Turn one corridor-separating wall into a Decode-locked door."""
    rnd = random.Random(seed)
    H, W = len(g), len(g[0])
    cand = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1)
            if g[y][x] == '#'
            and ((g[y][x - 1] == '.' and g[y][x + 1] == '.')
                 or (g[y - 1][x] == '.' and g[y + 1][x] == '.'))]
    if cand:
        x, y = rnd.choice(cand)
        g[y][x] = 'k'


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


def _mount_wall(g, x, y, ch):
    """Put ch on the wall a dead-end tip faces (opposite its neck)."""
    neck = _dead_end_neighbor(g, x, y)
    if not neck:
        return False
    wx, wy = x + (x - neck[0]), y + (y - neck[1])
    if 0 <= wy < len(g) and 0 <= wx < len(g[0]) and g[wy][wx] == '#':
        g[wy][wx] = ch
        return True
    return False


def _take_tips(g, start, n):
    """Farthest-first dead-end tips that are still plain floor."""
    tips = [(x, y) for (x, y) in _far_dead_ends(g, start) if g[y][x] == '.']
    assert len(tips) >= n, f"maze has {len(tips)} tips, need {n}"
    return tips


def _maze_with_tips(W, H, seed, loops, min_tips):
    """Generate a maze that still has at least min_tips dead ends after
    loop-carving (loops erase dead ends, so retry nearby seeds)."""
    for attempt in range(50):
        g = _maze(W, H, seed + attempt * 1000, loops)
        if len(_far_dead_ends(g, (1, 1))) >= min_tips:
            return g
    raise AssertionError(f"no maze {W}x{H} seed~{seed} with {min_tips} tips")


def _lvl3():
    # First maze: tight corridors, Heal book deep inside, push-block exit lock.
    g = _maze_with_tips(17, 11, seed=33, loops=2, min_tips=3)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 3)
    tx, ty = tips[0]
    g[ty][tx] = 'C'                        # Spellbook: Heal
    sx, sy = tips[1]
    g[sy][sx] = 'S'                        # note
    cols = [c for c in _exit_columns(g) if c <= len(g[0]) - 6]
    _append_block_exit(g, cols[len(cols) // 2])
    return dict(level=3, rows=_rows(g), theme="maze")


def _lvl4():
    # Maze with pit traps in dead ends; teleporter jumps deep to the HP gem.
    g = _maze_with_tips(21, 13, seed=44, loops=3, min_tips=6)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 6)
    lx, ly = tips[0]
    g[ly][lx] = 'L'                        # landing deep in the maze
    nb = _dead_end_neighbor(g, lx, ly)
    g[nb[1]][nb[0]] = 'C'                  # +5 Max HP gem beside the landing
    t2x, t2y = tips[1]
    g[t2y][t2x] = 'T'                      # teleporter pad far from the loot
    _sprinkle_pits(g, (1, 1), tips[2:5], 3)
    sx, sy = tips[5]
    g[sy][sx] = 'S'
    _secret_shortcuts(g, seed=45, count=2)
    cols = _exit_columns(g)
    _append_plain_exit(g, cols[-1])
    return dict(level=4, rows=_rows(g), theme="pitmaze")


def _lvl5():
    # Keeper's warren: merchant near the start, pits, Keeper guards the stairs.
    g = _maze_with_tips(21, 13, seed=55, loops=3, min_tips=6)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 6)
    mx, my = tips[-1]                      # nearest tip: merchant
    g[my][mx] = 'M'
    sx, sy = tips[0]
    g[sy][sx] = 'S'
    _sprinkle_pits(g, (1, 1), tips[1:5], 4)
    cols = _exit_columns(g)
    _append_plain_exit(g, cols[-1], door='K')   # Keeper blocks the doorway
    return dict(level=5, rows=_rows(g), theme="warren")


def _lvl6():
    # Key + alcove maze: find the key chest, feed the far alcove, gate opens.
    g = _maze_with_tips(23, 13, seed=66, loops=4, min_tips=6)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 6)
    cx_, cy_ = tips[0]
    g[cy_][cx_] = 'c'                      # key chest, deepest tip
    ax, ay = tips[1]
    assert _mount_wall(g, ax, ay, 'a')     # alcove wall at another far tip
    gx_, gy_ = tips[2]
    g[gy_][gx_] = 'C'                      # +1 Atk gem
    hx, hy = tips[-1]
    g[hy][hx] = 'H'                        # haybed near start
    zx, zy = tips[3]
    nb = _dead_end_neighbor(g, zx, zy)
    g[nb[1]][nb[0]] = 'Z'                  # static teaser (optional, needs Purge)
    sx, sy = tips[4]
    g[sy][sx] = 'S'
    cols = _exit_columns(g)
    _append_plain_exit(g, cols[-1], door='g')   # alcove opens this gate
    return dict(level=6, rows=_rows(g), theme="keymaze")


def _lvl7():
    # Illusion maze: secret walls, statics guard loot, button opens the exit.
    g = _maze_with_tips(23, 15, seed=77, loops=3, min_tips=7)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 7)
    bx, by = tips[0]
    assert _mount_wall(g, bx, by, '!')     # exit button at the deepest tip
    cx_, cy_ = tips[1]
    g[cy_][cx_] = 'C'                      # Spellbook: Purge (freely reachable)
    zx, zy = tips[2]
    nb = _dead_end_neighbor(g, zx, zy)
    g[zy][zx] = 'm'                        # mimic behind a static pile
    g[nb[1]][nb[0]] = 'Z'
    z2x, z2y = tips[3]
    g[z2y][z2x] = 'Z'
    mx, my = tips[-1]
    g[my][mx] = 'M'
    sx, sy = tips[4]
    g[sy][sx] = 'S'
    _secret_shortcuts(g, seed=78, count=3)
    _locked_shortcut(g, seed=79)           # Decode shortcut between corridors
    cols = _exit_columns(g)
    _append_plain_exit(g, cols[len(cols) // 2], door='g')  # button-gated exit
    return dict(level=7, rows=_rows(g), theme="illusion")


def _lvl8():
    # Deep maze: teleporter shortcut, mimic, def gem, push-block exit lock.
    g = _maze_with_tips(25, 15, seed=88, loops=4, min_tips=7)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 7)
    dx_, dy_ = tips[0]
    g[dy_][dx_] = 'C'                      # +1 Def gem
    mx, my = tips[1]
    g[my][mx] = 'm'                        # mimic ambush
    lx, ly = tips[2]
    g[ly][lx] = 'L'
    t_x, t_y = tips[-1]
    g[t_y][t_x] = 'T'                      # near-start pad jumps deep
    zx, zy = tips[3]
    nb = _dead_end_neighbor(g, zx, zy)
    g[nb[1]][nb[0]] = 'Z'                  # static guards a pit-free cul-de-sac
    sx, sy = tips[4]
    g[sy][sx] = 'S'
    _secret_shortcuts(g, seed=89, count=2)
    cols = [c for c in _exit_columns(g) if c <= len(g[0]) - 6]
    _append_block_exit(g, cols[len(cols) // 2])
    return dict(level=8, rows=_rows(g), theme="deepmaze")


def _lvl9():
    # Hardest maze: pits, statics on gold, mimic, secret walls, block lock.
    g = _maze_with_tips(25, 15, seed=99, loops=2, min_tips=8)
    g[1][1] = '@'
    tips = _take_tips(g, (1, 1), 8)
    gx_, gy_ = tips[0]
    nb = _dead_end_neighbor(g, gx_, gy_)
    g[gy_][gx_] = 'C'                      # gold cache
    g[nb[1]][nb[0]] = 'Z'                  # ...behind a static pile
    mx, my = tips[1]
    g[my][mx] = 'm'                        # mimic
    _sprinkle_pits(g, (1, 1), tips[2:6], 4)
    sx, sy = tips[6]
    g[sy][sx] = 'S'
    _secret_shortcuts(g, seed=98, count=3)
    cols = [c for c in _exit_columns(g) if c <= len(g[0]) - 6]
    _append_block_exit(g, cols[-1])
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


_WALKABLE_CHARS = set('.,@BpgTOcCLm<XKSH12345Z%')
_FACING_VALUE = {"FACING_NORTH": 0, "FACING_EAST": 1,
                 "FACING_SOUTH": 2, "FACING_WEST": 3}


def _start_facing(rows, start):
    """Face the player toward an open corridor (mazes often wall the east)."""
    sx, sy = start
    for name, (dx, dy) in (("FACING_EAST", (1, 0)), ("FACING_SOUTH", (0, 1)),
                           ("FACING_NORTH", (0, -1)), ("FACING_WEST", (-1, 0))):
        nx, ny = sx + dx, sy + dy
        if 0 <= ny < len(rows) and 0 <= nx < len(rows[ny]) \
                and rows[ny][nx] in _WALKABLE_CHARS:
            return name
    return "FACING_EAST"


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
    start_facing = _start_facing(rows, start)

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
            nfac = _FACING_VALUE[_start_facing(LEVELS[level]["rows"], (nsx, nsy))]
            cmds = [
                '{ .type=CMD_DISPLAY_MESSAGE, .message={ "DESCENDING...", 20 } }',
                f'{{ .type=CMD_CHANGE_MAP, .change_map={{ {nxt}, {nsx}, {nsy}, {nfac} }} }}',
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
                events=events, start=start, evs=evs, dialogs=dialogs,
                start_facing=start_facing)


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
        f"#define MAP{level}_START_FACING {b['start_facing']}",
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
