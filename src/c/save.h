#pragma once
#include "pebble.h"

#define SAVE_KEY_BASE     1      // key 1 = main save, key 2+ = map deltas
#define SAVE_KEY_PAID     12     // anti-farm paid-kill blob
#define SAVE_VERSION      4
#define SAVE_MAX_MAPS     10     // max maps we can store deltas for (10-level campaign)
#define SAVE_MAX_TILE_DELTAS  4  // tile edits are rare (no scripted CMD_CHANGE_TILE)
#define SAVE_MAX_DELTAS      42  // decor: block-push trails need headroom

typedef struct {
    int8_t x, y;
    uint8_t tile_type;
} TileDelta;

typedef struct {
    int8_t x, y;
    uint8_t decor_type;
} DecorDelta;

// Main save — player state only, no deltas
typedef struct {
    uint8_t version;
    int8_t  x, y;
    uint8_t facing;
    uint8_t map_id;
    int8_t  hp, max_hp;
    int8_t  mp, max_mp;
    int8_t  def, dex;
    int8_t  bonus_atk, bonus_def;
    int16_t gold;
    uint8_t weapon, armor;
    uint8_t spellbook;
    int8_t  respawn_x, respawn_y;
    uint8_t respawn_map;
    uint8_t respawn_facing;
    int8_t  inventory[16];
    uint8_t flags[32];          // 256 flags bitpacked
} SaveData;

// Per-map delta record — stored in separate persist key per map
typedef struct {
    uint8_t    map_id;
    uint8_t    tile_delta_count;
    uint8_t    decor_delta_count;
    TileDelta  tile_deltas[SAVE_MAX_TILE_DELTAS];
    DecorDelta decor_deltas[SAVE_MAX_DELTAS];
} MapDeltaData;

bool save_exists(void);
void save_write(void);
bool save_read(void);
void save_delete(void);

// call from map_set_tile / map_set_decor
void save_record_tile (int x, int y, uint8_t tile_type);
void save_record_decor(int x, int y, uint8_t decor_type);

// called when changing maps
void save_flush_map_deltas(uint8_t map_id);
void save_load_map_deltas(uint8_t map_id);

// In save.h, add the getter:
bool save_is_loading_deltas(void);

