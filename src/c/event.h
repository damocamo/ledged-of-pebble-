#pragma once
#include "pebble.h"
#include "map.h" 
#include "dialog.h" 


// Command types — add more as you build them out
typedef enum {
    CMD_DISPLAY_MESSAGE = 0,
    CMD_CHANGE_TILE     = 1,
    CMD_CHANGE_DECOR    = 2,
    CMD_FLAG_CHECK      = 3,  // if flag is set, jump to event or stop
    CMD_FLAG_CHANGE     = 4,  // set or clear a flag
    CMD_DIALOG = 5,
    CMD_GIVE_ITEM       = 6,  // add this
    CMD_USE_ITEM = 7,  // add to enum
    CMD_ITEM_CHECK = 8,  // if player has item, consume it and continue. else jump/stop
    CMD_TAKE_ITEM = 9,
    CMD_SHAKE = 10,
    CMD_BATTLE = 11,
    CMD_HEAL_HP = 12,
    CMD_MOVE_PLAYER = 13,
    CMD_SET_WEAPON = 14,
    CMD_SET_ARMOR  = 15,
    CMD_SET_FAIL_EVENT = 16,
    CMD_DISPLAY_ICON = 17,
    CMD_CHANGE_MAP = 18,
    CMD_JUMP = 19,
    CMD_SET_RESPAWN = 20,  // ← ADD THIS
    CMD_DAMAGE = 21,       // deal damage to the player (e.g. pit falls)
    CMD_OPEN_SHOP = 22,    // open merchant shop UI
    CMD_SET_SPELLBOOK = 23,
    CMD_GRANT_BONUS = 24,  // permanent gem: kind 0=HP 1=MP 2=ATK 3=DEF
    CMD_ADD_GOLD = 25,
} CmdType;

typedef enum {
    NO_EVENT_TRIGGER      = -1,
    EVENT_TRIGGER_STEP    =  0,  // fires when player steps on the tile
    EVENT_TRIGGER_FORWARD =  1,  // fires when player walks into the tile
    EVENT_TRIGGER_ITEM    = 2,  // fires only when used from inventory
} EventTrigger;

typedef struct {
    int x;
    int y;
} ShakeOffset;

// A single command with its data
typedef struct {
    CmdType type;
    union {
        struct {
            const char *text;
            int         ticks;   // 0 = persistent until next event
        } message;
        struct {
            int     x, y;
            uint8_t tile_type;
        } change_tile;
        struct {
            int     x, y;
            uint8_t decor_type;
        } change_decor;
        struct {
            uint8_t flag_id;
            int8_t  jump_event;  // event to run if flag is SET
                                 // -1 = stop current event
                                 // -2 = no match (same as false, keep going)
        } flag_check;
        struct {
            uint8_t flag_id;
            bool    value;
        } flag_change;
        struct {
            const DialogDef *def;
        } dialog;
        struct {
            int8_t slot;
            int8_t quantity;
        } give_item;
        struct {
            int8_t  item_slot;      // which item is being used
            uint8_t required_decor; // what decor must be in front
            int8_t  success_event;  // event to run on success
            int8_t  fail_event;     // event to run on fail, -1 = silent
        } use_item;
        struct {
            int8_t  item_slot;
            int8_t  jump_event;  // event to run if item NOT found, -1 = stop, -2 = continue anyway
        } item_check;
        struct {
            int8_t slot;
            int8_t quantity;
        } take_item;
        struct {
            uint8_t duration_ms;  // total shake time in 100ms units, e.g. 10 = 1 second
            uint8_t intensity;    // max pixel offset (1-4)
        } shake;
        struct {
            uint8_t enemy_id;
            int8_t  victory_event;  // -1 = no post-battle event
            bool    can_run;        // can player run from this battle?
        } battle;
        struct {
            int8_t amount;  // hp to restore, -1 = full heal
        } heal_hp;
        struct {
            int8_t  x;
            int8_t  y;
            int8_t  facing;  // -1 = keep current facing
        } move_player;
        struct { uint8_t weapon_id; } set_weapon;
        struct { uint8_t armor_id;  } set_armor;
        struct {
            int8_t event_id;  // event to jump to on next item check fail
        } set_fail_event;
        struct {
            uint8_t icon_slot;  // slot in icon atlas
            int     ticks;      // display duration, same as message
        } display_icon;
        struct {
            uint8_t map_id;
            int8_t  x;
            int8_t  y;
            uint8_t facing;  // FACING_NORTH/EAST/SOUTH/WEST, or 0xFF to keep current
        } change_map;
        struct {
            int8_t event_id;  // event to jump to
        } jump;
        struct {
            // No parameters needed - uses current position
        } set_respawn;
        struct {
            int8_t amount;  // hp to remove (before armor); respawns if it kills
        } damage;
        struct {
            uint8_t spell_id;  // SpellId
        } set_spellbook;
        struct {
            uint8_t kind;    // 0=max_hp, 1=max_mp, 2=atk, 3=def
            int8_t  amount;
        } grant_bonus;
        struct {
            int16_t amount;
        } add_gold;
    };
} EventCmd;

// An event is a list of commands
typedef struct {
    const EventCmd *cmds;
    int             count;
    EventTrigger     trigger;
} Event;

#define FLAG_COUNT 128  // expand as needed

// Add these globals
extern const EventCmd *s_pending_cmds;
extern int s_pending_cmd_index;
extern int s_pending_cmd_count;

void event_resume(void);  // call this when dialog closes

extern ShakeOffset g_shake_offset;
bool event_shake_active(void);

void event_start_shake(uint8_t duration_ticks, uint8_t intensity);

void  flag_set(uint8_t flag_id, bool value);
bool  flag_get(uint8_t flag_id);
void  flags_clear(void);
void event_check_item(int player_x, int player_y, Facing facing, int8_t failed_event);

void event_init(Layer *canvas);

// Call this to fire an event by ID
void event_run(int event_id);
void start_event_run(void);

void event_load_map(int map_id);
void event_check_forward(int player_x, int player_y, Facing facing);

// Call in your draw loop to render any active message
void event_draw_message(GBitmap *fb);
void event_draw_icon(GBitmap *fb);

void event_check_tile(int map_x, int map_y);

// Call in window_unload to clean up timer
void event_cleanup(void);