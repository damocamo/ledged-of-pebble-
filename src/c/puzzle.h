#pragma once
#include "pebble.h"
#include "map.h"

// ============================================================
// puzzle.c / puzzle.h
// Grimrock-style grid puzzle mechanics that the event VM cannot
// express directly: pushable blocks and held pressure plates.
//
// Teleporters, pits, item alcoves and secret buttons are authored
// purely with the existing event system (CMD_MOVE_PLAYER,
// CMD_CHANGE_MAP, CMD_CHANGE_TILE/DECOR, CMD_USE_ITEM, CMD_DAMAGE);
// this module only handles the two mechanics that need per-move
// simulation and direct world state.
// ============================================================

// A pressure plate wired to a target cell. A plate is "pressed" when the
// player stands on it OR a pushable block rests on it. While pressed the
// target is set to its ON state; while released it reverts to OFF.
//
// Use PUZZLE_KEEP (0xFF) for on_decor/off_decor/on_tile/off_tile to leave
// that layer untouched, and flag_id < 0 for "no flag".
#define PUZZLE_KEEP 0xFF

typedef struct {
    int8_t  plate_x, plate_y;   // where the plate sits
    int8_t  target_x, target_y; // cell the plate drives (e.g. a gate)
    uint8_t on_decor, off_decor;// decor applied to target when pressed / released
    uint8_t on_tile,  off_tile; // tile  applied to target when pressed / released
    int8_t  flag_id;            // optional flag set(true)/clear(false), -1 = none
} PlateLink;

// Select the plate-link table for a map. Call after a map's tiles/decor and
// saved deltas are loaded. Safe for maps with no plates.
void puzzle_load_map(uint8_t map_id);

// Try to push a block that sits directly in front of the player.
// (px,py) = player position, facing = direction being pressed.
// Returns true if a block moved (the caller should then advance the player
// into the vacated tile). Returns false if there is no block in front, or it
// cannot move (blocked by wall/decor/edge).
bool puzzle_try_push(int px, int py, Facing facing);

// Re-derive every pressure plate's state from the current block positions and
// the player's tile, applying each plate's ON/OFF target state. Call after any
// move/push and once whenever a map is (re)entered. Plate-driven changes are
// written live-only so they never persist and are always recomputed.
void puzzle_recompute_plates(int player_x, int player_y);
