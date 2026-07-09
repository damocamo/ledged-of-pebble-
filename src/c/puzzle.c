#include "pebble.h"
#include "puzzle.h"
#include "map.h"
#include "event.h"   // flag_set

// Per-map pressure-plate link tables. Generated alongside the campaign maps so
// plate coordinates stay in sync with the level layouts.
#include "puzzle_links.inc"

static const PlateLink *s_links = NULL;
static int              s_link_count = 0;

void puzzle_load_map(uint8_t map_id) {
    if (map_id < PUZZLE_MAP_COUNT) {
        s_links      = PUZZLE_TABLES[map_id];
        s_link_count = PUZZLE_TABLE_COUNTS[map_id];
    } else {
        s_links      = NULL;
        s_link_count = 0;
    }
}

// Is (x,y) authored as a pressure plate on the current map?
static bool is_plate(int x, int y) {
    for (int i = 0; i < s_link_count; i++) {
        if (s_links[i].plate_x == x && s_links[i].plate_y == y) return true;
    }
    return false;
}

// A block can only be pushed onto empty floor or onto a pressure plate.
static bool push_dest_free(int x, int y) {
    if (!map_in_bounds(x, y)) return false;
    uint8_t t = map_get_tile(x, y);
    if (t != TILE_FLOOR && t != TILE_FLOOR_R) return false;
    uint8_t d = map_get_decor(x, y);
    return d == DECOR_NONE || d == DECOR_PLATE_UP || d == DECOR_PLATE_DOWN;
}

static void facing_delta(Facing f, int *dx, int *dy) {
    *dx = 0; *dy = 0;
    switch (f) {
        case FACING_NORTH: *dy = -1; break;
        case FACING_EAST:  *dx =  1; break;
        case FACING_SOUTH: *dy =  1; break;
        case FACING_WEST:  *dx = -1; break;
    }
}

bool puzzle_try_push(int px, int py, Facing facing) {
    int dx, dy;
    facing_delta(facing, &dx, &dy);

    int bx = px + dx, by = py + dy;          // tile holding the block
    if (map_get_decor(bx, by) != DECOR_BLOCK) return false;

    int nx = bx + dx, ny = by + dy;          // tile the block would move into
    if (!push_dest_free(nx, ny)) return false;

    // Move the block (persisted via delta). Restore the vacated tile: if it was
    // a plate, put the plate decor back so it is still detectable.
    map_set_decor(nx, ny, DECOR_BLOCK);
    map_set_decor(bx, by, is_plate(bx, by) ? DECOR_PLATE_UP : DECOR_NONE);
    return true;
}

void puzzle_recompute_plates(int player_x, int player_y) {
    for (int i = 0; i < s_link_count; i++) {
        const PlateLink *L = &s_links[i];

        uint8_t cur      = map_get_decor(L->plate_x, L->plate_y);
        bool    block_on = (cur == DECOR_BLOCK);
        bool    player_on= (player_x == L->plate_x && player_y == L->plate_y);
        bool    pressed  = block_on || player_on;

        // Update the plate's own visual, unless a block is covering it.
        if (!block_on) {
            map_set_decor_live(L->plate_x, L->plate_y,
                               pressed ? DECOR_PLATE_DOWN : DECOR_PLATE_UP);
        }

        // Drive the linked target. Written live-only so it never persists.
        if (pressed) {
            if (L->on_decor != PUZZLE_KEEP)
                map_set_decor_live(L->target_x, L->target_y, L->on_decor);
            if (L->on_tile != PUZZLE_KEEP)
                map_set_tile_live(L->target_x, L->target_y, L->on_tile);
            if (L->flag_id >= 0) flag_set((uint8_t)L->flag_id, true);
        } else {
            if (L->off_decor != PUZZLE_KEEP)
                map_set_decor_live(L->target_x, L->target_y, L->off_decor);
            if (L->off_tile != PUZZLE_KEEP)
                map_set_tile_live(L->target_x, L->target_y, L->off_tile);
            if (L->flag_id >= 0) flag_set((uint8_t)L->flag_id, false);
        }
    }
}
