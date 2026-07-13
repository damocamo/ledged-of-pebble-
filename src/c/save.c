#include "pebble.h"
#include "save.h"
#include "player.h"
#include "map.h"
#include "event.h"
#include "encounter.h"
#include "puzzle.h"
#include "minimap.h"

// ============================================================
// save.c
// Persistent save system with per-map delta tracking
// ============================================================
//
// SAVE SYSTEM OVERVIEW:
// The save system consists of two parts:
// 1. Global save data (player stats, inventory, flags, position)
// 2. Per-map deltas (tile/decor changes for each map)
//
// PERSIST KEY LAYOUT:
// - Key 1 (SAVE_KEY_BASE): Main save data (SaveData struct)
// - Key 2: Map 0 deltas (prison)
// - Key 3: Map 1 deltas (catacombs)
// - Key 4+: Future map deltas
//
// This separation allows:
// - Map changes persist even after leaving the map
// - Each map maintains its own state independently
// - Player can return to previous maps with all changes intact
//
// DELTA TRACKING:
// "Deltas" are changes made to the map from its initial state:
// - Opened gates (decor change)
// - Removed chests (decor change)
// - Moved pillars (tile change)
// - Triggered switches (decor change)
//
// Delta recording flow:
// 1. Event system calls map_set_tile() or map_set_decor()
// 2. Those functions call save_record_tile()/save_record_decor()
// 3. Changes accumulate in s_current_deltas buffer
// 4. On map switch or save, deltas flush to persistent storage
// 5. On map load, deltas apply back to restore state
//
// DELTA DEDUPLICATION:
// If the same position is modified multiple times:
// - Check if (x,y) already has a delta recorded
// - If yes: Update the tile/decor value in place
// - If no: Add new delta to buffer
// This prevents duplicate entries for the same location.
//
// LOADING PROTECTION:
// The s_loading_deltas flag prevents re-recording during load:
// - When loading deltas, s_loading_deltas = true
// - map_set_tile/decor() still get called to apply changes
// - save_record_tile/decor() early-return if s_loading_deltas is true
// - Without this, loading would re-record all deltas, causing duplication
//
// MAP SWITCHING FLOW:
// When player moves between maps via CMD_CHANGE_MAP:
// 1. save_flush_map_deltas(old_map_id) - Save current map's changes
// 2. map_init_for(new_map_id) - Load new map's base tiles/decor
// 3. event_load_map(new_map_id) - Load new map's events
// 4. save_load_map_deltas(new_map_id) - Apply saved changes to new map
//
// FLAG SYSTEM:
// Flags track game state (switches, chests opened, dialogs seen, etc.):
// - 256 flags stored as 32 bytes (1 bit per flag)
// - pack_flags(): Convert 256 bools → 32 bytes for storage
// - unpack_flags(): Convert 32 bytes → 256 bools on load
// - Flags 0-25:  Map 1 events
// - Flags 30-62: Map 2 events
//
// SAVE DATA STRUCTURE:
// SaveData contains:
// - Player position (x, y, facing, map_id) - 4 bytes
// - Player stats (hp, max_hp, def, dex) - 4 bytes
// - Equipment (weapon, armor) - 2 bytes
// - Respawn checkpoint (respawn_x, respawn_y, respawn_map, respawn_facing) - 4 bytes
// - Inventory (16 slots, quantity per slot) - 16 bytes
// - Flags (256 flags packed into 32 bytes) - 32 bytes
// - Version number (for compatibility checking) - 1 byte
// - total bytes - 63 bytes
//
// RESPAWN SYSTEM:
// Respawn points set via CMD_SET_RESPAWN (usually at haybeds):
// - Saves current position as respawn checkpoint
// - On death in combat, player_respawn() is called
// - Player returns to last respawn point with full HP
// - All progress (flags, inventory, map changes) preserved
// - Only position resets to checkpoint
//
// SAVE/LOAD TIMING:
// Saves occur automatically:
// - After each successful forward/back step (and block push)
// - After shop purchases
// - After combat victory / defeat (and early Select dismiss)
// - On app exit (window_unload), unless still on the title screen
// - When changing maps (flushes current map's deltas)
//
// Load occurs:
// - On Continue from the title screen
// - Restores player state, map state, deltas, paid-kill counters
//
// VERSION CHECKING:
// Save data includes SAVE_VERSION constant:
// - If loaded version != current version, save is deleted
// - Prevents crashes from struct layout changes
// - Forces new game start after code updates that change SaveData
//
// BUFFER LIMITS:
// - SAVE_MAX_TILE_DELTAS: 4 tile changes (scripted tile edits unused)
// - SAVE_MAX_DELTAS: 42 decor changes per map (block-push trails)
// - SAVE_MAX_MAPS: 10 maps supported (delta keys 2-11)
// - FLAG_COUNT: 128 flags (packed into SaveData.flags[32], 256-bit capacity)
// - INVENTORY_SLOTS: 16 inventory slots
// - Minimap exploration: persist keys 20-29 (one per map)
//
// If delta buffer fills, new changes are silently dropped (logged as warning).
// Persistant Storage is 256 Bytes per key.
// A tile/delta is x, y, and tile ints for 3 bytes each. Other information saved
// is mapid, tile delta count, and decor delta count for 3 bytes as well.
// 256 - 3 bytes + (3 * 42) + (3 * 42) = 1 byte left.
// 255 bytes fits inside of 256 persistant storage.
//
// DEBUGGING:
// Commented-out APP_LOG statements throughout for debugging:
// - Uncomment to see delta recording/loading in detail
// - Shows exact tile/decor changes being saved/restored
// - Useful for tracking down map state issues
//
// DELETE BEHAVIOR:
// save_delete() clears:
// - Main save data (key 1)
// - All map deltas (keys 2-11)
// - Minimap exploration (keys 20-29)
// - In-memory delta buffer
// Player must start new game from beginning.
//
// ============================================================
 
static MapDeltaData s_current_deltas = {0};

static bool s_loading_deltas = false;

// Persist key for a map's deltas — key 1 = player, key 2+ = map 0,1,2...
static int delta_key(uint8_t map_id) {
    return SAVE_KEY_BASE + 1 + map_id;  // key 2 = map 0, key 3 = map 1, etc
}

void save_record_tile(int x, int y, uint8_t tile_type) {
    if (s_loading_deltas) return;

    if (s_current_deltas.tile_delta_count >= SAVE_MAX_TILE_DELTAS) {
        //APP_LOG(APP_LOG_LEVEL_WARNING, "Tile delta buffer full!");
        return;
    }
    
    // Check if already recorded
    for (int i = 0; i < s_current_deltas.tile_delta_count; i++) {
        if (s_current_deltas.tile_deltas[i].x == x && 
            s_current_deltas.tile_deltas[i].y == y) {
            s_current_deltas.tile_deltas[i].tile_type = tile_type;
            //APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated tile delta (%d,%d) = %d", x, y, tile_type);
            return;
        }
    }
    
    s_current_deltas.tile_deltas[s_current_deltas.tile_delta_count++] =
         (TileDelta){ x, y, tile_type };
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Recorded tile delta (%d,%d) = %d", x, y, tile_type);
}

void save_record_decor(int x, int y, uint8_t decor_type) {
        if (s_loading_deltas) return;  

    if (s_current_deltas.decor_delta_count >= SAVE_MAX_DELTAS) {
        //APP_LOG(APP_LOG_LEVEL_WARNING, "Decor delta buffer full!");
        return;
    }
    
    // Check if already recorded
    for (int i = 0; i < s_current_deltas.decor_delta_count; i++) {
        if (s_current_deltas.decor_deltas[i].x == x && 
            s_current_deltas.decor_deltas[i].y == y) {
            s_current_deltas.decor_deltas[i].decor_type = decor_type;
            //APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated decor delta (%d,%d) = %d", x, y, decor_type);
            return;
        }
    }
    
    s_current_deltas.decor_deltas[s_current_deltas.decor_delta_count++] =
         (DecorDelta){ x, y, decor_type };
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Recorded decor delta (%d,%d) = %d", x, y, decor_type);
}

void save_flush_map_deltas(uint8_t map_id) {
    uint8_t key = 2 + map_id;
    
    s_current_deltas.map_id = map_id;

    int written = persist_write_data(key, &s_current_deltas, sizeof(MapDeltaData));
    (void)written;
    //APP_LOG(APP_LOG_LEVEL_INFO, "Wrote %d bytes to persist key %d", sizeof(MapDeltaData), key);
    
    // Clear deltas after flushing
    //memset(&s_current_deltas, 0, sizeof(MapDeltaData));
    //APP_LOG(APP_LOG_LEVEL_INFO, "=== Flush Complete ===");
}

bool save_is_loading_deltas(void) {
    return s_loading_deltas;
}

void save_load_map_deltas(uint8_t map_id) {
    uint8_t key = 2 + map_id;

    memset(&s_current_deltas, 0, sizeof(MapDeltaData));
    s_current_deltas.map_id = map_id;
    
    int read = persist_read_data(key, &s_current_deltas, sizeof(MapDeltaData));
    (void)read;

    if (!persist_exists(key)) {
        //APP_LOG(APP_LOG_LEVEL_INFO, "No deltas found for map %d (key %d)", map_id, key);
        return;
    }

    // Clamp counts — stale/corrupt persist data must never index past the arrays.
    if (s_current_deltas.tile_delta_count > SAVE_MAX_TILE_DELTAS)
        s_current_deltas.tile_delta_count = SAVE_MAX_TILE_DELTAS;
    if (s_current_deltas.decor_delta_count > SAVE_MAX_DELTAS)
        s_current_deltas.decor_delta_count = SAVE_MAX_DELTAS;
    
    //APP_LOG(APP_LOG_LEVEL_INFO, "=== Loading Map %d Deltas ===", map_id);
    //APP_LOG(APP_LOG_LEVEL_INFO, "Tile deltas: %d", s_current_deltas.tile_delta_count);
    //APP_LOG(APP_LOG_LEVEL_INFO, "Decor deltas: %d", s_current_deltas.decor_delta_count);

    s_loading_deltas = true; 
    
    // Apply and log each delta
    for (int i = 0; i < s_current_deltas.tile_delta_count; i++) {
        TileDelta *td = &s_current_deltas.tile_deltas[i];
        map_set_tile(td->x, td->y, td->tile_type);
        //APP_LOG(APP_LOG_LEVEL_INFO, "  Applied Tile (%d,%d) -> %d", td->x, td->y, td->tile_type);
    }
    
    for (int i = 0; i < s_current_deltas.decor_delta_count; i++) {
        DecorDelta *dd = &s_current_deltas.decor_deltas[i];
        map_set_decor(dd->x, dd->y, dd->decor_type);
        //APP_LOG(APP_LOG_LEVEL_INFO, "  Applied Decor (%d,%d) -> %d", dd->x, dd->y, dd->decor_type);
    }

    s_loading_deltas = false;
    
    //APP_LOG(APP_LOG_LEVEL_INFO, "=== Map %d Deltas Applied ===", map_id);
}

// ---- Flag packing -------------------------------------------------------
// Store 256 flags in 32 bytes using bitpacking
static void pack_flags(uint8_t *out) {
    memset(out, 0, 32);
    for (int i = 0; i < FLAG_COUNT && i < 256; i++)
        if (flag_get(i)) out[i/8] |= (1 << (i%8));
}

static void unpack_flags(const uint8_t *in) {
    // Always clear first — otherwise stale in-RAM flags survive a load.
    flags_clear();
    for (int i = 0; i < FLAG_COUNT; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        bool is_set = (in[byte_idx] >> bit_idx) & 1;
        flag_set(i, is_set);
    }
}

// ---- Public API ---------------------------------------------------------
bool save_exists(void) {
    return persist_exists(SAVE_KEY_BASE);
}

void save_write(void) {
    SaveData s = {0};
    s.version     = SAVE_VERSION;
    s.x           = (int8_t)g_player.x;
    s.y           = (int8_t)g_player.y;
    s.facing      = (uint8_t)g_player.facing;
    s.map_id      = (uint8_t)g_player.map_id;
    s.hp          = g_player.hp;
    s.max_hp      = g_player.max_hp;
    s.mp          = g_player.mp;
    s.max_mp      = g_player.max_mp;
    s.def         = g_player.def;
    s.dex         = g_player.dex;
    s.bonus_atk   = g_player.bonus_atk;
    s.bonus_def   = g_player.bonus_def;
    s.gold        = g_player.gold;
    s.weapon      = g_player.weapon;
    s.armor       = g_player.armor;
    s.spellbook   = g_player.spellbook;
    s.respawn_x   = g_player.respawn_x;
    s.respawn_y   = g_player.respawn_y;
    s.respawn_map = g_player.respawn_map;
    s.respawn_facing = g_player.respawn_facing;

    for (int i = 0; i < INVENTORY_SLOTS && i < 16; i++)
        s.inventory[i] = g_player.inventory[i].quantity;

    pack_flags(s.flags);

    persist_write_data(SAVE_KEY_BASE, &s, sizeof(SaveData));

    // also flush current map deltas
    save_flush_map_deltas((uint8_t)g_player.map_id);

    // persist explored minimap tiles so "Continue" restores them
    minimap_persist_flush();

    // anti-farm paid-kill counters
    persist_write_data(SAVE_KEY_PAID,
                       encounter_paid_kills_blob(),
                       encounter_paid_kills_size());
}

bool save_read(void) {
    if (!persist_exists(SAVE_KEY_BASE)) return false;

    SaveData s = {0};
    int read = persist_read_data(SAVE_KEY_BASE, &s, sizeof(SaveData));

    if (read <= 0 || s.version != SAVE_VERSION) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "save_read: bad data v=%d", s.version);
        // Wipe all related keys so a corrupt base save can't leave orphans.
        save_delete();
        return false;
    }   

    g_player.x           = s.x;
    g_player.y           = s.y;
    g_player.facing      = (Facing)s.facing;
    g_player.map_id      = s.map_id;
    g_player.hp          = s.hp;
    g_player.max_hp      = s.max_hp;
    g_player.mp          = s.mp;
    g_player.max_mp      = s.max_mp;
    g_player.def         = s.def;
    g_player.dex         = s.dex;
    g_player.bonus_atk   = s.bonus_atk;
    g_player.bonus_def   = s.bonus_def;
    g_player.gold        = s.gold;
    g_player.weapon      = s.weapon;
    g_player.armor       = s.armor;
    g_player.spellbook   = s.spellbook;
    g_player.respawn_x   = s.respawn_x;
    g_player.respawn_y   = s.respawn_y;
    g_player.respawn_map = s.respawn_map;
    g_player.respawn_facing = (Facing)s.respawn_facing;

    map_init_for(g_player.map_id);
    event_load_map(g_player.map_id);
    encounter_init(g_player.map_id);

    for (int i = 0; i < INVENTORY_SLOTS && i < 16; i++)
        g_player.inventory[i].quantity = s.inventory[i];

    unpack_flags(s.flags);

    // load deltas for the player's current map
    save_load_map_deltas(s.map_id);

    // re-derive pressure-plate state from the restored block/player positions
    puzzle_recompute_plates(g_player.x, g_player.y);

    // restore anti-farm counters
    if (persist_exists(SAVE_KEY_PAID)) {
        uint8_t blob[32];
        int n = persist_read_data(SAVE_KEY_PAID, blob, sizeof(blob));
        encounter_load_paid_kills(blob, n);
    } else {
        encounter_clear_paid_kills();
    }

    return true;
}

void save_delete(void) {
    persist_delete(SAVE_KEY_BASE);
    persist_delete(SAVE_KEY_PAID);
    // delete all map delta keys
    for (int i = 0; i < SAVE_MAX_MAPS; i++)
        persist_delete(delta_key(i));
    memset(&s_current_deltas, 0, sizeof(MapDeltaData));
    // clear persisted exploration so a New Game starts with a blank map
    minimap_wipe();
    encounter_clear_paid_kills();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "save_delete: all cleared");
}