#pragma once
#include "pebble.h"
#include "combat.h"
#include "map1.h"
#include "map2.h"
#include "map3.h"
#include "map4.h"
#include "map5.h"
#include "map6.h"
#include "map7.h"
#include "map8.h"
#include "map9.h"
#include "map10.h"

// ---- Dimensions ---------------------------------------------------------
#define MAP_W  MAP1_W   // 27
#define MAP_H  MAP1_H   // 25

// ---- Tile types ---------------------------------------------------------
#define TILE_BLANK    0
#define TILE_FLOOR    1
#define TILE_FLOOR_R  2
#define TILE_WALL     3
#define TILE_DOOR     4
#define TILE_PILLAR   5
#define TILE_SECRET   6   // renders as a wall but is walkable (illusory wall)

#define TILE_TYPE_COUNT 7

// ---- Decor types --------------------------------------------------------
#define DECOR_NONE          0
#define DECOR_DOOR          1
#define DECOR_CHEST         2
#define DECOR_HAYBED        3
#define DECOR_SKULL         4
#define DECOR_KEYHOLE       5
#define DECOR_GATE          6
#define DECOR_SWITCH_DOWN   7
#define DECOR_SWITCH_UP     8
#define DECOR_MAREN         9
#define DECOR_F_SWITCH     10
// ---- Grimrock-style puzzle props ---------------------------------------
#define DECOR_BLOCK        11   // pushable block (solid)
#define DECOR_PLATE_UP     12   // pressure plate, released
#define DECOR_PLATE_DOWN   13   // pressure plate, pressed
#define DECOR_TELEPORT     14   // teleporter pad
#define DECOR_PIT          15   // open pit / trapdoor
#define DECOR_ALCOVE_EMPTY 16   // wall alcove, awaiting an item
#define DECOR_ALCOVE_FULL  17   // wall alcove, item placed
#define DECOR_STATIC_PILE  18   // Signal static — Purge clears to floor
#define DECOR_LOCKED_DOOR  19   // locked door — Decode opens

#define DECOR_TYPE_COUNT   20

// ---- View slots ---------------------------------------------------------
#define VIEW_SLOT_COUNT 11

#define MAX_MAP_ENEMIES 4

// ---- Structs ------------------------------------------------------------
typedef struct {
    int16_t src_x, src_y, src_w, src_h;  // atlas coords/size
    int16_t dest_x;                      // screen X offset
    int8_t  dest_y;                      // screen Y offset (-100..76)
    bool flip;
    bool draw_ceiling;
} TileView;

typedef struct {
    int16_t src_x, src_y, src_w, src_h;
    int16_t dest_x;
    int8_t  dest_y;
    bool flip;
    bool draw_ceiling;
} DecorView;

typedef struct {
    EnemyId enemies[MAX_MAP_ENEMIES];
    uint8_t enemy_count;
} MapEncounterTable;

typedef struct {
    int8_t dx, dy;
} Offset;

typedef enum {
    FACING_NORTH = 0,
    FACING_EAST  = 1,
    FACING_SOUTH = 2,
    FACING_WEST  = 3,
} Facing;

// ---- Map data (read-only) -----------------------------------------------
extern const uint8_t s_map[MAP_H][MAP_W];
extern const uint8_t s_decor_map[MAP_H][MAP_W];

extern uint8_t s_map_live[MAP_H][MAP_W];
extern uint8_t s_decor_map_live[MAP_H][MAP_W];

void map_init_for(uint8_t map_id);
void map_set_tile(int x, int y, uint8_t tile_type);
void map_set_decor(int x, int y, uint8_t decor_type);

// Live-only writes: change the working map WITHOUT recording a save delta.
// Used by the puzzle system for transient state (pressure plates, plate-driven
// gates) so plate effects are recomputed on load rather than persisted.
void map_set_tile_live(int x, int y, uint8_t tile_type);
void map_set_decor_live(int x, int y, uint8_t decor_type);

// Total number of maps registered in the campaign.
int  map_count(void);

// ---- View tables (read-only) --------------------------------------------
extern const Offset   VIEW_OFFSETS[VIEW_SLOT_COUNT][4];
extern const TileView TILE_VIEWS[TILE_TYPE_COUNT][VIEW_SLOT_COUNT];
extern const DecorView DECOR_VIEWS[DECOR_TYPE_COUNT][VIEW_SLOT_COUNT];

// ---- Map queries --------------------------------------------------------
bool map_is_walkable(int x, int y);
uint8_t map_get_tile(int x, int y);
uint8_t map_get_decor(int x, int y);
bool map_in_bounds(int x, int y);
