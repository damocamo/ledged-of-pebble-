#include "pebble.h"
#include "map.h"
#include "map1.h"
#include "map2.h"
#include "save.h"

#define TV_NONE { 0, 0, 0, 0, 0, 0, false, false }
#define DV_NONE { 0, 0, 0, 0, 0, 0, false, false }

uint8_t s_map_live[MAP_H][MAP_W];
uint8_t s_decor_map_live[MAP_H][MAP_W];

// ---- View offsets -------------------------------------------------------
const Offset VIEW_OFFSETS[VIEW_SLOT_COUNT][4] = {
    { {-2,-2}, { 2,-2}, { 2, 2}, {-2, 2} },
    { { 2,-2}, { 2, 2}, {-2, 2}, {-2,-2} },
    { {-1,-2}, { 2,-1}, { 1, 2}, {-2, 1} },
    { { 1,-2}, { 2, 1}, {-1, 2}, {-2,-1} },
    { { 0,-2}, { 2, 0}, { 0, 2}, {-2, 0} },
    { {-1,-1}, { 1,-1}, { 1, 1}, {-1, 1} },
    { { 1,-1}, { 1, 1}, {-1, 1}, {-1,-1} },
    { { 0,-1}, { 1, 0}, { 0, 1}, {-1, 0} },
    { {-1, 0}, { 0,-1}, { 1, 0}, { 0, 1} },
    { { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1} },
    { { 0, 0}, { 0, 0}, { 0, 0}, { 0, 0} },
};

// ---- Tile views ---------------------------------------------------------
const TileView TILE_VIEWS[TILE_TYPE_COUNT][VIEW_SLOT_COUNT] = {
    // TILE_BLANK (0)
    {
        TV_NONE, TV_NONE, TV_NONE, TV_NONE, TV_NONE,
        TV_NONE, TV_NONE, TV_NONE,
        TV_NONE, TV_NONE, TV_NONE,
    },
    // TILE_FLOOR (1)
    {
        { 33, 0, 13, 11,   0, 34, false, true },
        { 33, 0, 13, 11, 234, 34,  true, true },
        { 60, 0, 49, 13,   0, 32, false, true },
        { 60, 0, 49, 13, 162, 32,  true, true },
        {109, 0, 46, 13,  84, 32, false, true },
        {142,36, 43, 26,   0, 44, false, true },
        {142,36, 43, 26, 174, 44,  true, true },
        { 64,36, 78, 26,  52, 44, false, true },
        {130,62, 27, 27,   0, 76, false, true },
        {130,62, 27, 27, 206, 76,  true, true },
        {  0,62,130, 27,   0, 76, false, true },
    },
    // TILE_FLOOR_R (2)
    {
        { 33, 0, 13, 11,   0, 34, false, true },
        { 33, 0, 13, 11, 234, 34,  true, true },
        { 60, 0, 49, 13,   0, 32, false, true },
        { 60, 0, 49, 13, 162, 32,  true, true },
        {109, 0, 46, 13,  84, 32,  true, true },
        {142,36, 43, 26,   0, 44, false, true },
        {142,36, 43, 26, 174, 44,  true, true },
        { 64,36, 78, 26,  52, 44,  true, true },
        {130,62, 27, 27,   0, 76, false, true },
        {130,62, 27, 27, 206, 76,  true, true },
        {  0,62,130, 27,   0, 76,  true, true },
    },
    // TILE_WALL (3)
    {
        {149, 89, 13, 28,   0,  0, false, true },
        {149, 89, 13, 28, 234,  0,  true, true },
        {114,172, 49, 29,   0,  0, false, true },
        {114,172, 49, 29, 162,  0,  true, true },
        {114,202, 46, 29,  84,  0,  true, true },
        { 43,123, 43, 48,   0,  0, false, true },
        { 43,123, 43, 48, 174,  0,  true, true },
        { 86,123, 78, 48,  52,  0, false, true },
        { 42,171, 27, 60,   0,  0, false, true },
        { 42,171, 27, 60, 206,  0,  true, true },
        {  0,62, 130, 27,   0, 76, false, true },
    },
    // TILE_DOOR (4)
    {
        TV_NONE, TV_NONE, TV_NONE, TV_NONE, TV_NONE,
        TV_NONE, TV_NONE, TV_NONE,
        TV_NONE, TV_NONE, TV_NONE,
    },
    // TILE_PILLAR (5)
    {
        { 47,  0, 13, 16,   0, 24, false, true },
        { 47,  0, 13, 16, 234, 24,  true, true },
        {202,107, 49, 29,   0,  0, false, true },
        {202,107, 49, 29, 162,  0,  true, true },
        {201,  0, 46, 29,  84,  0,  true, true },
        {  0,123, 43, 48,   0,  0, false, true },
        {  0,123, 43, 48, 174,  0,  true, true },
        {  0,280, 78, 48,  52,  0, false, true },
        {130, 62, 27, 27,   0, 76, false, true },
        {130, 62, 27, 27, 206, 76,  true, true },
        {  0, 62,130, 27,   0, 76,  true, true },
    },
    // TILE_SECRET (6) — renders identically to TILE_WALL but is walkable
    {
        {149, 89, 13, 28,   0,  0, false, true },
        {149, 89, 13, 28, 234,  0,  true, true },
        {114,172, 49, 29,   0,  0, false, true },
        {114,172, 49, 29, 162,  0,  true, true },
        {114,202, 46, 29,  84,  0,  true, true },
        { 43,123, 43, 48,   0,  0, false, true },
        { 43,123, 43, 48, 174,  0,  true, true },
        { 86,123, 78, 48,  52,  0, false, true },
        { 42,171, 27, 60,   0,  0, false, true },
        { 42,171, 27, 60, 206,  0,  true, true },
        {  0,62, 130, 27,   0, 76, false, true },
    },
};

// ---- Decor views --------------------------------------------------------
const DecorView DECOR_VIEWS[DECOR_TYPE_COUNT][VIEW_SLOT_COUNT] = {
    // DECOR_NONE (0)
    {
        DV_NONE, DV_NONE, DV_NONE, DV_NONE, DV_NONE,
        DV_NONE, DV_NONE, DV_NONE,
        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_DOOR (1)
    {
        { 204,137, 13, 29,   0,-14, false, false },
        { 204,137, 13, 29, 234,-14,  true, false },
        { 164,149, 40, 34,  16,-22, false, false },
        { 164,149, 40, 34, 164,-22,  true, false },
        { 167, 62, 23, 34, 106,-22, false, false },
        {  70,172, 43, 58,   0,-38, false, false },
        {  70,172, 43, 58, 174,-38,  true, false },
        { 217,136, 39, 57,  90,-38, false, false },
        {  22,171, 20, 92,   0,-64, false, false },
        {  22,171, 20, 92, 220,-64,  true, false },
        DV_NONE,
    },
    // DECOR_CHEST (2)
    {
        DV_NONE, DV_NONE,
        { 84, 14, 28, 21,  20,  4, false, false },
        { 84, 14, 28, 21, 184,  4,  true, false },
        {112, 14, 24, 21, 106,  4, false, false },
        {118, 89, 30, 32,   0,  4, false, false },
        {118, 89, 30, 32, 200,  4,  true, false },
        {164,183, 38, 32,  92,  4, false, false },
        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_HAYBED (3)
    {
        {186,  0,  8, 14,   0, 16, false, false },
        {155,  0,  8, 14, 244, 16, false, false },
        {155,  0, 39, 14,  20, 16, false, false },
        {155,  0, 39, 14, 172, 16, false, false },
        {155,  0, 39, 14,  92, 16, false, false },
        {167, 14, 34, 22,   0, 30, false, false },
        {136, 14, 34, 22, 192, 30, false, false },
        {136, 14, 65, 22,  62, 30, false, false },
        DV_NONE, DV_NONE,
        {  0, 89,104, 30,  24, 70, false, false },
    },
    // DECOR_SKULL (4)
    {
        { 23,  0,  8, 11,   0, 22, false, false },
        {  0,  0,  8, 11, 244, 22, false, false },
        {  0,  0, 31, 11,  26, 22, false, false },
        {  0,  0, 31, 11, 172, 22, false, false },
        {  0,  0, 31, 11,  98, 22, false, false },
        { 12, 14, 32, 15,   0, 40, false, false },
        {  0, 14, 32, 15, 196, 40, false, false },
        {  0, 14, 44, 15,  86, 40, false, false },
        DV_NONE, DV_NONE,
        {  0, 36, 64, 22,  67, 84, false, false },
    },
    // DECOR_KEYHOLE (5)
    {
        DV_NONE, DV_NONE,
        { 54, 18, 30, 10,  32,-10, false, false },
        { 54, 18, 30, 10, 168,-10,  true, false },
        { 54, 18, 10, 10, 120,-10, false, false },
        { 53,231, 35, 17,   0,-16, false, false },
        { 53,231, 35, 17, 190,-16,  true, false },
        { 42,231, 17, 17, 112,-16, false, false },
        { 44, 15, 10, 18,   4,-16, false, false },
        { 44, 15, 10, 18, 236,-16,  true, false },
        DV_NONE,
    },
    // DECOR_GATE (6)
    {
        DV_NONE, DV_NONE,
        {164,108, 38, 40,   8,-40, false, false },
        {164,108, 38, 40, 176,-40,  true, false },
        {164,108, 38, 40,  92,-40, false, false },
        {220, 36, 36, 71,   0,-70, false, false },
        {220, 36, 36, 71, 188,-70,  true, false },
        {190, 36, 66, 71,  64,-70, false, false },
        {  6,175, 16,100,   0,-100, false, false },
        {  6,175, 16,100, 228,-100,  true, false },
        DV_NONE,
    },
    // DECOR_SWITCH_DOWN (7)
    {
        {231,219,  4,  8,   8,-10, false, false },
        {231,219,  4,  8, 244,-10,  true, false },
        {202,217, 33, 15,  34,-14, false, false },
        {202,217, 33, 15, 160,-14,  true, false },
        {202,217,  8, 15, 122,-14, false, false },
        {234,194,  8, 12,  68,-12, false, false },
        {234,194,  8, 12, 176,-12,  true, false },
        {202,194, 14, 20, 114,-16, false, false },
        {217,194, 16, 23,   4,-22, false, false },
        {217,194, 16, 23, 224,-22,  true, false },
        DV_NONE,
    },
    // DECOR_SWITCH_UP (8)
    {
        {231,219,  4,  8,   8,  6, false, true },
        {231,219,  4,  8, 244,  6,  true, true },
        {202,217, 33, 15,  34, -2, false, true },
        {202,217, 33, 15, 160, -2,  true, true },
        {202,217,  8, 15, 122, -2, false, true },
        {234,194,  8, 12,  68, -4, false, true },
        {234,194,  8, 12, 176, -4,  true, true },
        {202,194, 14, 20, 114, -6, false, true },
        {217,194, 16, 23,   4, -8, false, true },
        {217,194, 16, 23, 224, -8,  true, true },
        DV_NONE,
    },
    // DECOR_MAREN (9)
    {
        DV_NONE, DV_NONE, DV_NONE, DV_NONE, 
        {239,215, 17, 36,  113,  -26, false, false },

        DV_NONE, DV_NONE,
        {192,274, 26, 56,  104,  -46, false, false },

        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_F_SWITCH (9)
    {
        DV_NONE, DV_NONE, DV_NONE, DV_NONE, 
        {43,257, 18, 5,  116,  34, false, false },

        {22,269, 27, 9,  0,  48, false, false }, 
        {22,269, 27, 9,  206,  48, true, false },
        {43,248, 22, 8,  116,  48, false, false },

        DV_NONE, DV_NONE, DV_NONE,
    },
    // ---- Puzzle props ---------------------------------------------------
    // NOTE: the following seven entries reuse existing atlas regions as
    // placeholder art so the mechanics are visible and the build is valid.
    // Replace the source rectangles with dedicated sprites in a later art pass.

    // DECOR_BLOCK (11) — placeholder: reuse the CHEST sprite (reads as a crate)
    {
        DV_NONE, DV_NONE,
        { 84, 14, 28, 21,  20,  4, false, false },
        { 84, 14, 28, 21, 184,  4,  true, false },
        {112, 14, 24, 21, 106,  4, false, false },
        {118, 89, 30, 32,   0,  4, false, false },
        {118, 89, 30, 32, 200,  4,  true, false },
        {164,183, 38, 32,  92,  4, false, false },
        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_PLATE_UP (12) — placeholder: reuse the floor-switch sprite
    {
        DV_NONE, DV_NONE, DV_NONE, DV_NONE,
        {43,257, 18, 5,  116,  34, false, false },
        {22,269, 27, 9,    0,  48, false, false },
        {22,269, 27, 9,  206,  48,  true, false },
        {43,248, 22, 8,  116,  48, false, false },
        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_PLATE_DOWN (13) — placeholder: floor-switch sprite, nudged down 2px
    {
        DV_NONE, DV_NONE, DV_NONE, DV_NONE,
        {43,257, 18, 5,  116,  36, false, false },
        {22,269, 27, 9,    0,  50, false, false },
        {22,269, 27, 9,  206,  50,  true, false },
        {43,248, 22, 8,  116,  50, false, false },
        DV_NONE, DV_NONE, DV_NONE,
    },
    // DECOR_TELEPORT (14) — placeholder: reuse the SKULL floor decal
    {
        { 23,  0,  8, 11,   0, 22, false, false },
        {  0,  0,  8, 11, 244, 22, false, false },
        {  0,  0, 31, 11,  26, 22, false, false },
        {  0,  0, 31, 11, 172, 22, false, false },
        {  0,  0, 31, 11,  98, 22, false, false },
        { 12, 14, 32, 15,   0, 40, false, false },
        {  0, 14, 32, 15, 196, 40, false, false },
        {  0, 14, 44, 15,  86, 40, false, false },
        DV_NONE, DV_NONE,
        {  0, 36, 64, 22,  67, 84, false, false },
    },
    // DECOR_PIT (15) — placeholder: reuse the HAYBED floor mass
    {
        {186,  0,  8, 14,   0, 16, false, false },
        {155,  0,  8, 14, 244, 16, false, false },
        {155,  0, 39, 14,  20, 16, false, false },
        {155,  0, 39, 14, 172, 16, false, false },
        {155,  0, 39, 14,  92, 16, false, false },
        {167, 14, 34, 22,   0, 30, false, false },
        {136, 14, 34, 22, 192, 30, false, false },
        {136, 14, 65, 22,  62, 30, false, false },
        DV_NONE, DV_NONE,
        {  0, 89,104, 30,  24, 70, false, false },
    },
    // DECOR_ALCOVE_EMPTY (16) — placeholder: reuse the KEYHOLE wall niche
    {
        DV_NONE, DV_NONE,
        { 54, 18, 30, 10,  32,-10, false, false },
        { 54, 18, 30, 10, 168,-10,  true, false },
        { 54, 18, 10, 10, 120,-10, false, false },
        { 53,231, 35, 17,   0,-16, false, false },
        { 53,231, 35, 17, 190,-16,  true, false },
        { 42,231, 17, 17, 112,-16, false, false },
        { 44, 15, 10, 18,   4,-16, false, false },
        { 44, 15, 10, 18, 236,-16,  true, false },
        DV_NONE,
    },
    // DECOR_ALCOVE_FULL (17) — wall niche (same as ALCOVE_EMPTY). Chest art
    // here read as unreachable loot inside the wall; the opening gate plus
    // "THE GATE OPENS" message is the insert feedback instead.
    {
        DV_NONE, DV_NONE,
        { 54, 18, 30, 10,  32,-10, false, false },
        { 54, 18, 30, 10, 168,-10,  true, false },
        { 54, 18, 10, 10, 120,-10, false, false },
        { 53,231, 35, 17,   0,-16, false, false },
        { 53,231, 35, 17, 190,-16,  true, false },
        { 42,231, 17, 17, 112,-16, false, false },
        { 44, 15, 10, 18,   4,-16, false, false },
        { 44, 15, 10, 18, 236,-16,  true, false },
        DV_NONE,
    },
    // DECOR_STATIC_PILE (18) — reuse skull pile art
    {
        DV_NONE, DV_NONE,
        {155,  0, 39, 14, 172, 16, false, false },
        {155,  0, 39, 14,  92, 16, false, false },
        {167, 14, 34, 22,   0, 30, false, false },
        {136, 14, 34, 22, 192, 30, false, false },
        {136, 14, 65, 22,  62, 30, false, false },
        DV_NONE, DV_NONE,
        {  0, 89,104, 30,  24, 70, false, false },
    },
    // DECOR_LOCKED_DOOR (19) — reuse door / keyhole niche
    {
        DV_NONE, DV_NONE,
        { 54, 18, 30, 10,  32,-10, false, false },
        { 54, 18, 30, 10, 168,-10,  true, false },
        { 54, 18, 10, 10, 120,-10, false, false },
        { 53,231, 35, 17,   0,-16, false, false },
        { 53,231, 35, 17, 190,-16,  true, false },
        { 42,231, 17, 17, 112,-16, false, false },
        { 44, 15, 10, 18,   4,-16, false, false },
        { 44, 15, 10, 18, 236,-16,  true, false },
        DV_NONE,
    },
};

// ---- Map query functions ------------------------------------------------

void map_set_tile(int x, int y, uint8_t tile_type) {
    if (!map_in_bounds(x, y)) return;
    s_map_live[y][x] = tile_type;
    save_record_tile(x, y, tile_type);
}

void map_set_decor(int x, int y, uint8_t decor_type) {
    if (!map_in_bounds(x, y)) return;
    s_decor_map_live[y][x] = decor_type;
    save_record_decor(x, y, decor_type);
}

// Live-only writes (no delta recorded) — see map.h.
void map_set_tile_live(int x, int y, uint8_t tile_type) {
    if (!map_in_bounds(x, y)) return;
    s_map_live[y][x] = tile_type;
}

void map_set_decor_live(int x, int y, uint8_t decor_type) {
    if (!map_in_bounds(x, y)) return;
    s_decor_map_live[y][x] = decor_type;
}

// ---- Map registry -------------------------------------------------------
// All maps share MAP_W/MAP_H (28x28); tiles/decor are memcpy'd into the live
// working buffers, so every map's arrays must be exactly that size.
typedef struct {
    const uint8_t (*tiles)[MAP_W];
    const uint8_t (*decor)[MAP_W];
} MapData;

static const MapData s_maps[] = {
    { MAP1_TILES,  MAP1_DECOR  },
    { MAP2_TILES,  MAP2_DECOR  },
    { MAP3_TILES,  MAP3_DECOR  },
    { MAP4_TILES,  MAP4_DECOR  },
    { MAP5_TILES,  MAP5_DECOR  },
    { MAP6_TILES,  MAP6_DECOR  },
    { MAP7_TILES,  MAP7_DECOR  },
    { MAP8_TILES,  MAP8_DECOR  },
    { MAP9_TILES,  MAP9_DECOR  },
    { MAP10_TILES, MAP10_DECOR },
};

#define MAP_REGISTRY_COUNT ((int)(sizeof(s_maps) / sizeof(s_maps[0])))

int map_count(void) { return MAP_REGISTRY_COUNT; }

void map_init_for(uint8_t map_id) {
    memset(s_map_live, 0, sizeof(s_map_live));
    memset(s_decor_map_live, 0, sizeof(s_decor_map_live));

    if (map_id >= MAP_REGISTRY_COUNT) map_id = 0;

    memcpy(s_map_live,       s_maps[map_id].tiles, sizeof(s_map_live));
    memcpy(s_decor_map_live, s_maps[map_id].decor, sizeof(s_decor_map_live));
}

bool map_in_bounds(int x, int y) {
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H;
}

// Update getters to use live copies
uint8_t map_get_tile(int x, int y) {
    if (!map_in_bounds(x, y)) return TILE_BLANK;
    return s_map_live[y][x];
}

uint8_t map_get_decor(int x, int y) {
    if (!map_in_bounds(x, y)) return DECOR_NONE;
    return s_decor_map_live[y][x];
}

bool map_is_walkable(int x, int y) {
    if (!map_in_bounds(x, y)) return false;
    uint8_t t = s_map_live[y][x];
    uint8_t d = s_decor_map_live[y][x];
    if (d == DECOR_GATE || d == DECOR_MAREN || d == DECOR_BLOCK
        || d == DECOR_STATIC_PILE || d == DECOR_LOCKED_DOOR) return false;
    return t == TILE_FLOOR || t == TILE_FLOOR_R || t == TILE_SECRET;
}