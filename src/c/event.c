#include "pebble.h"
#include "event.h"
#include "bitfont.h"
#include "map.h"  // for MAP_W, MAP_H
#include "dialog.h"
#include "player.h"
#include "menu.h"
#include "combat.h"
#include "map1events.inc"
#include "map2events.inc"
#include "map3events.inc"
#include "map4events.inc"
#include "map5events.inc"
#include "map6events.inc"
#include "map7events.inc"
#include "map8events.inc"
#include "map9events.inc"
#include "map10events.inc"
#include "save.h"
#include "encounter.h"
#include "puzzle.h"
#include "shop.h"

// ============================================================
// event.c
// Core event scripting system for dungeon interactions
// ============================================================
//
// EVENT SYSTEM OVERVIEW:
// Events are scripted sequences that control game logic, storytelling,
// and world interactions. Each event is a list of commands that execute
// in order, similar to a simple programming language.
//
// WHAT ARE EVENTS:
// Events are triggered by:
// - Stepping on a tile (EVENT_TRIGGER_STEP)
// - Interacting with object in front (EVENT_TRIGGER_FORWARD)
// - Using an item on object (EVENT_TRIGGER_ITEM)
// - Other events jumping to them (CMD_JUMP)
// - Victory conditions (combat_start victory_event parameter)
//
// EVENT STRUCTURE:
// Each event is an array of EventCmd structs:
// static const EventCmd s_event_5_cmds[] = {
//     { .type = CMD_FLAG_CHECK,  .flag_check  = { FLAG_CHEST_1, -1 } },
//     { .type = CMD_GIVE_ITEM,   .give_item   = { ITEM_SLOT_POTION, 1 } },
//     { .type = CMD_DISPLAY_ICON,.display_icon= { 9, 30 } },
//     { .type = CMD_DISPLAY_MESSAGE,.message  = { "FOUND POTION", 30 } },
//     { .type = CMD_FLAG_CHANGE, .flag_change = { FLAG_CHEST_1, true } },
//     { .type = CMD_CHANGE_DECOR,.change_decor= { 5, 8, DECOR_NONE } },
// };
//
// COMMAND TYPES:
// Flow control:
//   CMD_FLAG_CHECK - Skip to event if flag is set, else continue
//   CMD_FLAG_CHANGE - Set or clear a flag
//   CMD_JUMP - Jump to different event unconditionally
//   CMD_SET_FAIL_EVENT - Set fallback event for failed checks
//
// Display:
//   CMD_DISPLAY_MESSAGE - Show text message (timed or permanent)
//   CMD_DISPLAY_ICON - Show large icon (items, status indicators)
//   CMD_DIALOG - Open multi-page dialog (pauses event execution)
//
// World changes:
//   CMD_CHANGE_TILE - Modify map tile (floors, walls, pillars)
//   CMD_CHANGE_DECOR - Modify decorations (chests, gates, switches)
//   CMD_SHAKE - Screen shake effect (intensity, duration)
//   CMD_CHANGE_MAP - Switch to different map
//
// Player:
//   CMD_MOVE_PLAYER - Teleport player (x, y, facing)
//   CMD_HEAL_HP - Restore health (-1 = full heal)
//   CMD_SET_WEAPON - Equip weapon
//   CMD_SET_ARMOR - Equip armor
//   CMD_SET_RESPAWN - Save current position as checkpoint
//
// Items:
//   CMD_GIVE_ITEM - Add item to inventory
//   CMD_TAKE_ITEM - Remove item from inventory
//   CMD_ITEM_CHECK - Skip to event if player lacks item
//   CMD_USE_ITEM - Check for item + correct decor, consume on success
//
// Combat:
//   CMD_BATTLE - Start combat (enemy_id, victory_event, can_run)
//
// EXECUTION FLOW:
// Events use a pending command system:
// 1. event_run(id) is called
// 2. Sets s_pending_cmds to the event's command array
// 3. event_resume() executes commands sequentially
// 4. Some commands pause execution (CMD_DIALOG, CMD_BATTLE)
// 5. When those systems finish, they call event_resume() to continue
//
// This allows: Dialog → Give Item → Change Map all in one event.
//
// PAUSE & RESUME:
// Commands that take control (dialog, combat):
// - Call 'return' instead of 'break' to stop event execution
// - Store remaining commands in s_pending_cmds
// - When finished, call event_resume() to continue from where paused
//
// Example flow:
// Event 10: [CHECK FLAG] → [DIALOG] → [GIVE ITEM] → [CHANGE DECOR]
//           ↓             ↓          (paused)
//           Execute       Opens      
//                        dialog      
//                         ↓
//                    Player closes
//                    dialog_input_next()
//                         ↓
//                    event_resume()
//                         ↓
//                    [GIVE ITEM] → [CHANGE DECOR] execute
//
// JUMP LOGIC:
// CMD_FLAG_CHECK and CMD_ITEM_CHECK return jump codes:
// - jump = -2: Continue to next command (condition passed)
// - jump = -1: Stop event immediately (flag set, jump_event = -1)
// - jump >= 0: Jump to that event (flag set, jump to different event)
//
// Example state-based NPC dialog:
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_STAGE_3, 58 } }, // if stage 3, jump event 58
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_STAGE_2, 57 } }, // if stage 2, jump event 57
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_STAGE_1, 56 } }, // if stage 1, jump event 56
// { .type = CMD_JUMP, .jump = { 55 } },  // else jump to intro (event 55)
//
// FLAGS:
// 256 boolean flags track game state:
// - Which chests opened (FLAG_MAP2_CHEST1, etc.)
// - Which switches pulled (FLAG_MAP2_SWITCH1, etc.)
// - Story progression (FLAG_FREED_MAREN, FLAG_WARDEN_DEFEATED)
// - Dialog stages (FLAG_JOURNAL_MAREN, FLAG_SUBJECT6_MAREN)
//
// Flag organization:
// - Flags 0-29:  Map 1 (Prison)
// - Flags 30-62: Map 2 (Catacombs)
// - Flags 70+: Reserved for Map 3
//
// FAIL EVENTS:
// CMD_SET_FAIL_EVENT sets a fallback for failed checks:
// { .type = CMD_SET_FAIL_EVENT, .set_fail_event = { 53 } },
// { .type = CMD_ITEM_CHECK, .item_check = { ITEM_SLOT_KEY, -1 } },
//
// If player lacks key, jump to event 53 (failure message).
// This avoids repeating jump_event parameter for every check.
// Useful if the fail event is on a different index for each map.
//
// EVENT MAPS:
// Each map has an event grid (28×28 arrays):
// - s_event_map: Maps tile positions to event IDs
//
// Example: Chest at (15,3) has event_id = 10 in event map.
// When player steps on (15,3), event 10 runs (opens chest).
//
// EVENT TRIGGERS:
// - EVENT_TRIGGER_STEP: Fires when player steps on tile
// - EVENT_TRIGGER_FORWARD: Fires when player presses SELECT facing tile
// - EVENT_TRIGGER_ITEM: Fires when player uses item on tile (via menu)
// - NO_EVENT_TRIGGER: Event only runs via jump (routing, branching)
//
// SHAKE EFFECTS:
// event_start_shake(duration_ticks, intensity):
// - duration_ticks: How many 80ms ticks to shake
// - intensity: Maximum pixel offset (1-5 typical)
// - Used for: Combat hits, earthquakes, explosions, impacts
// - Automatically stops after duration
//
// ICON DISPLAY:
// CMD_DISPLAY_ICON shows 64×64 scaled icon centered on screen:
// - Icon slot: Index in icon atlas (0-19)
// - Ticks: Duration in 100ms units (30 = 3 seconds)
// - Common icons: 9=potion, 11=key, 18=dagger, 19=page
// - Pairs with CMD_DISPLAY_MESSAGE for "found item" notifications
//
// MESSAGE DISPLAY:
// CMD_DISPLAY_MESSAGE shows text at bottom of screen:
// - Max 31 characters
// - Ticks: Duration in 100ms units (0 = permanent until next event)
// - Automatically centered 
// - use \n for new lines in the message.
// - Can combine with icon (icon above, text below, keep lines to 2 max if icon is displayed)
//
// MAP SWITCHING:
// CMD_CHANGE_MAP handles complete map transitions:
// 1. Flushes current map deltas to save
// 2. Changes player position and map_id
// 3. Loads new map tiles/decor/events
// 4. Loads saved deltas for new map
// 5. Runs new map's START_EVENT
//
// This preserves all progress: flags, inventory, map changes all persist.
//
// EXAMPLE EVENTS:
//
// Simple chest:
// { .type = CMD_FLAG_CHECK,     .flag_check    = { FLAG_CHEST_5, -1 } },
// { .type = CMD_GIVE_ITEM,      .give_item     = { ITEM_SLOT_POTION, 1 } },
// { .type = CMD_DISPLAY_ICON,   .display_icon  = { 9, 30 } },
// { .type = CMD_DISPLAY_MESSAGE,.message       = { "HEALTH POTION", 30 } },
// { .type = CMD_FLAG_CHANGE,    .flag_change   = { FLAG_CHEST_5, true } },
// { .type = CMD_CHANGE_DECOR,   .change_decor  = { 24, 26, DECOR_NONE } },
//
// Locked gate:
// { .type = CMD_SET_FAIL_EVENT, .set_fail_event = { 53 } },  // "locked" message
// { .type = CMD_FLAG_CHECK,     .flag_check     = { FLAG_GATE_1, -1 } },
// { .type = CMD_ITEM_CHECK,     .item_check     = { ITEM_SLOT_KEY, -1 } },
// { .type = CMD_TAKE_ITEM,      .take_item      = { ITEM_SLOT_KEY, 1 } },
// { .type = CMD_FLAG_CHANGE,    .flag_change    = { FLAG_GATE_1, true } },
// { .type = CMD_CHANGE_DECOR,   .change_decor   = { 21, 6, DECOR_NONE } },
// { .type = CMD_DISPLAY_MESSAGE,.message        = { "GATE OPENED", 30 } },
//
// Boss battle:
// { .type = CMD_FLAG_CHECK,     .flag_check = { FLAG_BOSS_DEFEATED, -1 } },
// { .type = CMD_DIALOG,         .dialog     = { &warden_intro_dialog } },
// { .type = CMD_BATTLE,         .battle     = { ENEMY_WARDEN, 60, false } },
// // Victory event (60):
// { .type = CMD_FLAG_CHANGE,    .flag_change = { FLAG_BOSS_DEFEATED, true } },
// { .type = CMD_DIALOG,         .dialog      = { &warden_defeat_dialog } },
// { .type = CMD_GIVE_ITEM,      .give_item   = { ITEM_SLOT_KEY, 1 } },
//
// Rest checkpoint:
// { .type = CMD_SET_RESPAWN },
// { .type = CMD_HEAL_HP,        .heal_hp = { -1 } },
// { .type = CMD_DISPLAY_MESSAGE,.message  = { "YOU RESTED.", 30 } },
//
// DEBUGGING:
// Commented APP_LOG statements throughout for debugging:
// - Uncomment in cmd_flag_check() to see flag evaluations
// - Uncomment in cmd_change_map() to see map transitions
// - Useful for tracking event flow issues
//
// EVENT FILES:
// Events defined per-map in separate .inc files:
// - map1events.inc: Prison events (33 events)
// - map2events.inc: Catacombs events (67 events)
// Each includes: flag enums, event command arrays, event table
//
// PERFORMANCE:
// - Events stored in const arrays (ROM, not RAM)
// - Only active event's commands held in s_pending_cmds
// - Flag array: 256 bytes in RAM
// - Event map: 784 bytes (28×28) per map
//
// ============================================================

#define MSG_MAX_LEN 32

static const Event *s_events = NULL;
static int EVENT_COUNT  = 0;  // set at init
static int START_EVENT  = 0;  // set at init

ShakeOffset g_shake_offset = {0, 0};

static uint8_t  s_shake_intensity  = 0;
static uint8_t  s_shake_remaining  = 0;
static AppTimer *s_shake_timer     = NULL;
static int8_t s_fail_event = -1;  // -1 = stop, set by CMD_SET_FAIL_EVENT

uint8_t s_event_map[MAP_H][MAP_W];

// ---- Map event registry -------------------------------------------------
// ARRAY_LENGTH is used for the count so the initializer stays a compile-time
// constant (a `const int` variable is not a constant expression in C).
typedef struct {
    const Event         *table;
    int                  count;
    int                  start;
    const int8_t       (*grid)[MAP_W];
} MapEventInfo;

static const MapEventInfo s_map_events[] = {
    { MAP1_EVENT_TABLE,  ARRAY_LENGTH(MAP1_EVENT_TABLE),  MAP1_START_EVENT,  MAP1_EVENTS  },
    { MAP2_EVENT_TABLE,  ARRAY_LENGTH(MAP2_EVENT_TABLE),  MAP2_START_EVENT,  MAP2_EVENTS  },
    { MAP3_EVENT_TABLE,  ARRAY_LENGTH(MAP3_EVENT_TABLE),  MAP3_START_EVENT,  MAP3_EVENTS  },
    { MAP4_EVENT_TABLE,  ARRAY_LENGTH(MAP4_EVENT_TABLE),  MAP4_START_EVENT,  MAP4_EVENTS  },
    { MAP5_EVENT_TABLE,  ARRAY_LENGTH(MAP5_EVENT_TABLE),  MAP5_START_EVENT,  MAP5_EVENTS  },
    { MAP6_EVENT_TABLE,  ARRAY_LENGTH(MAP6_EVENT_TABLE),  MAP6_START_EVENT,  MAP6_EVENTS  },
    { MAP7_EVENT_TABLE,  ARRAY_LENGTH(MAP7_EVENT_TABLE),  MAP7_START_EVENT,  MAP7_EVENTS  },
    { MAP8_EVENT_TABLE,  ARRAY_LENGTH(MAP8_EVENT_TABLE),  MAP8_START_EVENT,  MAP8_EVENTS  },
    { MAP9_EVENT_TABLE,  ARRAY_LENGTH(MAP9_EVENT_TABLE),  MAP9_START_EVENT,  MAP9_EVENTS  },
    { MAP10_EVENT_TABLE, ARRAY_LENGTH(MAP10_EVENT_TABLE), MAP10_START_EVENT, MAP10_EVENTS },
};

#define MAP_EVENT_REGISTRY_COUNT ((int)(sizeof(s_map_events)/sizeof(s_map_events[0])))

void event_load_map(int map_id) {
    s_fail_event = -1;  // reset context
    if (map_id < 0 || map_id >= MAP_EVENT_REGISTRY_COUNT) map_id = 0;

    const MapEventInfo *m = &s_map_events[map_id];
    s_events    = m->table;
    EVENT_COUNT = m->count;
    START_EVENT = m->start;
    memcpy(s_event_map, m->grid, sizeof(s_event_map));

    puzzle_load_map((uint8_t)map_id);
}

//static const int8_t (*s_event_map)[MAP_W]         = MAP1_EVENTS;

static char  s_message[MSG_MAX_LEN];
static bool  s_message_active = false;
static AppTimer *s_message_timer = NULL;
static Layer    *s_canvas_ref     = NULL;  // add this
/* Packed bitflags — saves ~112 bytes vs bool[FLAG_COUNT] */
static uint8_t s_flags[(FLAG_COUNT + 7) / 8];

void event_init(Layer *canvas) {
    s_canvas_ref = canvas;
}

static uint8_t   s_icon_slot   = 0;
static bool      s_icon_active = false;
static AppTimer *s_icon_timer  = NULL;

const EventCmd *s_pending_cmds = NULL;
int s_pending_cmd_index = 0;
int s_pending_cmd_count = 0;

extern GBitmap *s_icon_atlas;  // defined in main.c

static void icon_timer_cb(void *context) {
    s_icon_timer  = NULL;
    s_icon_active = false;
    s_message_active = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

static void cmd_display_icon(const EventCmd *cmd) {

    s_icon_slot   = cmd->display_icon.icon_slot;
    s_icon_active = true;

    if (s_icon_timer) {
        app_timer_cancel(s_icon_timer);
        s_icon_timer = NULL;
    }
    if (cmd->display_icon.ticks > 0) {
        uint32_t ms = (uint32_t)cmd->display_icon.ticks * 100;
        s_icon_timer = app_timer_register(ms, icon_timer_cb, NULL);
    }

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

static void cmd_set_fail_event(const EventCmd *cmd) {
    s_fail_event = cmd->set_fail_event.event_id;
}

static void message_timer_cb(void *context) {
    s_message_active = false;
    s_icon_timer = false;
    s_message_timer  = NULL;
    // trigger a redraw so the message clears
    if (s_canvas_ref) {
        layer_mark_dirty(s_canvas_ref);
    }
}


static void cmd_dialog(const EventCmd *cmd) {
    dialog_open(cmd->dialog.def);
}

static void cmd_change_tile(const EventCmd *cmd) {
    map_set_tile(cmd->change_tile.x, cmd->change_tile.y,
                 cmd->change_tile.tile_type);
}

static void cmd_change_decor(const EventCmd *cmd) {
    map_set_decor(cmd->change_decor.x, cmd->change_decor.y,
                  cmd->change_decor.decor_type);
}

static void cmd_display_message(const EventCmd *cmd) {
    strncpy(s_message, cmd->message.text, MSG_MAX_LEN - 1);
    s_message[MSG_MAX_LEN - 1] = '\0';
    s_message_active = true;

    if (s_message_timer) {
        app_timer_cancel(s_message_timer);
        s_message_timer = NULL;
    }

    if (cmd->message.ticks > 0) {
        uint32_t ms = (uint32_t)cmd->message.ticks * 100;
        s_message_timer = app_timer_register(ms, message_timer_cb, NULL);
    }
}

static void cmd_give_item(const EventCmd *cmd) {
    player_give_item(cmd->give_item.slot, cmd->give_item.quantity);
}

static void cmd_set_respawn(const EventCmd *cmd) {
    g_player.respawn_x = g_player.x;
    g_player.respawn_y = g_player.y;
    g_player.respawn_map = g_player.map_id;
    g_player.respawn_facing = g_player.facing;
}

static void cmd_damage(const EventCmd *cmd) {
    player_take_damage(cmd->damage.amount);
    if (player_is_dead()) {
        player_respawn();
    }
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

static void cmd_use_item(const EventCmd *cmd, int player_x, int player_y, Facing facing) {
    // check player has the item
    if (!player_has_item(cmd->use_item.item_slot)) {
        if (cmd->use_item.fail_event >= 0)
            event_run(cmd->use_item.fail_event);
        return;
    }

    // get tile in front of player
    int fx = player_x;
    int fy = player_y;
    switch (facing) {
        case FACING_NORTH: fy--; break;
        case FACING_EAST:  fx++; break;
        case FACING_SOUTH: fy++; break;
        case FACING_WEST:  fx--; break;
    }

    // check decor matches
    uint8_t decor = map_get_decor(fx, fy);
    if (decor != cmd->use_item.required_decor) {
        if (cmd->use_item.fail_event >= 0)
            event_run(cmd->use_item.fail_event);
        return;
    }

    // success — consume item and fire success event
    player_take_item(cmd->use_item.item_slot, 1);
    if (cmd->use_item.success_event >= 0)
        event_run(cmd->use_item.success_event);
}

static int cmd_item_check(const EventCmd *cmd) {
    if (player_has_item(cmd->item_check.item_slot)) {
        return -2;  // has item — continue
    }
    // use s_fail_event if set, otherwise use inline jump
    int8_t jump = (s_fail_event >= 0) ? s_fail_event
                                      : cmd->item_check.jump_event;
    //s_fail_event = -1;  // reset after use
    return jump;
}

static void cmd_take_item(const EventCmd *cmd) {
    player_take_item(cmd->take_item.slot, cmd->take_item.quantity);
}

static void cmd_heal_hp(const EventCmd *cmd) {
    if (cmd->heal_hp.amount == -1) {
        g_player.hp = g_player.max_hp;
        g_player.mp = g_player.max_mp;
    } else {
        player_heal(cmd->heal_hp.amount);
    }
}

static void cmd_set_spellbook(const EventCmd *cmd) {
    player_set_spellbook((SpellId)cmd->set_spellbook.spell_id);
}

static void cmd_grant_bonus(const EventCmd *cmd) {
    switch (cmd->grant_bonus.kind) {
        case 0: player_add_max_hp(cmd->grant_bonus.amount); break;
        case 1: player_add_max_mp(cmd->grant_bonus.amount); break;
        case 2: player_add_bonus_atk(cmd->grant_bonus.amount); break;
        case 3: player_add_bonus_def(cmd->grant_bonus.amount); break;
    }
}

static void cmd_add_gold(const EventCmd *cmd) {
    player_add_gold(cmd->add_gold.amount);
}

static void shake_tick(void *context) {
    s_shake_timer = NULL;
    if (s_shake_remaining == 0) {
        g_shake_offset.x = 0;
        g_shake_offset.y = 0;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }

    // random offset within intensity range
    int range = s_shake_intensity * 2 + 1;
    g_shake_offset.x = (rand() % range) - s_shake_intensity;
    g_shake_offset.y = (rand() % range) - s_shake_intensity;

    s_shake_remaining--;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);

    s_shake_timer = app_timer_register(80, shake_tick, NULL);
}

void event_start_shake(uint8_t duration_ticks, uint8_t intensity) {
    if (s_shake_timer) {
        app_timer_cancel(s_shake_timer);
        s_shake_timer = NULL;
    }
    s_shake_intensity = intensity;
    s_shake_remaining = duration_ticks;
    s_shake_timer = app_timer_register(80, shake_tick, NULL);
}

bool event_shake_active(void) {
    return s_shake_remaining > 0 || s_shake_timer != NULL;
}

static void cmd_move_player(const EventCmd *cmd) {
    g_player.x = cmd->move_player.x;
    g_player.y = cmd->move_player.y;
    if (cmd->move_player.facing >= 0)
        g_player.facing = (Facing)cmd->move_player.facing;

    // check for step events at new position
    event_check_tile(g_player.x, g_player.y);

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

static void cmd_shake(const EventCmd *cmd) {
    // cancel any existing shake
    if (s_shake_timer) {
        app_timer_cancel(s_shake_timer);
        s_shake_timer = NULL;
    }
    s_shake_intensity = cmd->shake.intensity;
    s_shake_remaining = cmd->shake.duration_ms;
    s_shake_timer = app_timer_register(80, shake_tick, NULL);
}

static void cmd_change_map(const EventCmd *cmd) {
    // flush current map deltas before switching
    save_flush_map_deltas(g_player.map_id);

    // switch map
    g_player.map_id = cmd->change_map.map_id;
    g_player.x      = cmd->change_map.x;
    g_player.y      = cmd->change_map.y;
    if (cmd->change_map.facing != 0xFF) {
        g_player.facing = (Facing)cmd->change_map.facing;
    }

    // init new map
    map_init_for(cmd->change_map.map_id);
    event_load_map(cmd->change_map.map_id);
    encounter_init(cmd->change_map.map_id);

    // load that map's saved deltas
    save_load_map_deltas(cmd->change_map.map_id);

    // derive pressure-plate state for the freshly loaded map
    puzzle_recompute_plates(g_player.x, g_player.y);

    start_event_run();
}


typedef enum {
    FLAG_SWITCH_1 = 0,
    FLAG_SWITCH_2 = 1,
    FLAG_CHEST_1 = 2,
    DOOR_FLAG_1 = 3,
    // add more here
} FlagId;

void flag_set(uint8_t flag_id, bool value) {
    if (flag_id >= FLAG_COUNT) return;
    uint8_t bit = (uint8_t)(1u << (flag_id & 7));
    if (value) s_flags[flag_id >> 3] |= bit;
    else       s_flags[flag_id >> 3] &= (uint8_t)~bit;
}

bool flag_get(uint8_t flag_id) {
    if (flag_id >= FLAG_COUNT) return false;
    return (s_flags[flag_id >> 3] & (uint8_t)(1u << (flag_id & 7))) != 0;
}

void flags_clear(void) {
    memset(s_flags, 0, sizeof(s_flags));
}

static int cmd_flag_check(const EventCmd *cmd) {
    int flag_id = cmd->flag_check.flag_id;
    bool flag_value = flag_get(flag_id);
    int jump_event = cmd->flag_check.jump_event;
    
    if (flag_get(cmd->flag_check.flag_id)) {
        return cmd->flag_check.jump_event;  // flag is set — jump or stop
    }
    return -2;  // flag is not set — continue
}

static void cmd_flag_change(const EventCmd *cmd) {
    flag_set(cmd->flag_change.flag_id, cmd->flag_change.value);
}

// ---- Public API ---------------------------------------------------------

void start_event_run(void) {
    event_run(START_EVENT);
}

void event_run(int event_id) {
    if (event_id < 0 || event_id >= EVENT_COUNT) return;

    const Event *ev = &s_events[event_id];
    s_pending_cmds = ev->cmds;
    s_pending_cmd_count = ev->count;
    s_pending_cmd_index = 0;

    event_resume();  // start executing
}

void event_resume(void) {

    //const Event *ev = &s_events[event_id];
    while (s_pending_cmd_index < s_pending_cmd_count) {
    //for (int i = 0; i < ev->count; i++) {

        const EventCmd *cmd = &s_pending_cmds[s_pending_cmd_index];
        //const EventCmd *cmd = &ev->cmds[i];

        s_pending_cmd_index++;

        int jump = -2;
        switch (cmd->type) {
            case CMD_DISPLAY_MESSAGE:
                cmd_display_message(cmd);
                break;
            case CMD_CHANGE_TILE:
                cmd_change_tile(cmd);
                break;
            case CMD_CHANGE_DECOR:
                cmd_change_decor(cmd);
                break;
            case CMD_FLAG_CHECK:
                jump = cmd_flag_check(cmd);
                break;
            case CMD_FLAG_CHANGE:
                cmd_flag_change(cmd);
                break;
            case CMD_DIALOG:
                cmd_dialog(cmd);
                return;  // always stop event processing — dialog takes over input
            case CMD_GIVE_ITEM:
                cmd_give_item(cmd);
                break;
            case CMD_USE_ITEM:
                cmd_use_item(cmd, g_player.x, g_player.y, g_player.facing);
                break;
            case CMD_ITEM_CHECK:
                jump = cmd_item_check(cmd);
                break;
            case CMD_TAKE_ITEM:
                cmd_take_item(cmd);
                break;
            case CMD_SHAKE:
                cmd_shake(cmd);
                break;
            case CMD_BATTLE:
                combat_start(cmd->battle.enemy_id, cmd->battle.victory_event, cmd->battle.can_run);
                return;  // stop event chain — combat takes over input
            case CMD_HEAL_HP:
                cmd_heal_hp(cmd);
                break;
            case CMD_MOVE_PLAYER:
                cmd_move_player(cmd);
                break;
            case CMD_SET_WEAPON:
                player_set_weapon(cmd->set_weapon.weapon_id);
                break;
            case CMD_SET_ARMOR:
                player_set_armor(cmd->set_armor.armor_id);
                break;
            case CMD_SET_FAIL_EVENT:
                cmd_set_fail_event(cmd);
                break;
            case CMD_DISPLAY_ICON:
                cmd_display_icon(cmd);
                break;
            case CMD_CHANGE_MAP:
                cmd_change_map(cmd);
                return;
            case CMD_JUMP:
                event_run(cmd->jump.event_id);
                return;  // stop current event chain
            case CMD_SET_RESPAWN:
                cmd_set_respawn(cmd);
                break;
            case CMD_DAMAGE:
                cmd_damage(cmd);
                break;
            case CMD_OPEN_SHOP:
                shop_open();
                return;  // shop takes over input
            case CMD_SET_SPELLBOOK:
                cmd_set_spellbook(cmd);
                break;
            case CMD_GRANT_BONUS:
                cmd_grant_bonus(cmd);
                break;
            case CMD_ADD_GOLD:
                cmd_add_gold(cmd);
                break;
        }

        if (jump == -1) return;          // stop event early
        if (jump >= 0)  {                // jump to another event
            event_run(jump);
            return;
        }
        // jump == -2: continue to next command
    }
}

#define DISPLAY_ICON_SIZE    16
#define DISPLAY_ICON_SCALE    4   // 64px — large and clear
#define DISPLAY_ICON_DRAW    (DISPLAY_ICON_SIZE * DISPLAY_ICON_SCALE)

void event_draw_icon(GBitmap *fb) {

    if (!s_icon_active || !s_icon_atlas) return;

    GRect fb_bounds = gbitmap_get_bounds(fb);
    int cx = fb_bounds.size.w / 2;
    int cy = fb_bounds.size.h / 2;

    // draw centered on screen
    int dest_x = cx - DISPLAY_ICON_DRAW / 2;
    int dest_y = cy - DISPLAY_ICON_DRAW / 2;

    if(s_message_active){
        dest_y = cy - 54;
    }

    uint8_t *data    = gbitmap_get_data(s_icon_atlas);
    int      stride  = gbitmap_get_bytes_per_row(s_icon_atlas);
    GColor  *palette = gbitmap_get_palette(s_icon_atlas);

    int src_x = s_icon_slot * DISPLAY_ICON_SIZE;

    for (int row = 0; row < DISPLAY_ICON_SIZE; row++) {
        for (int dy = 0; dy < DISPLAY_ICON_SCALE; dy++) {
            int fb_y = dest_y + row * DISPLAY_ICON_SCALE + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);

            for (int col = 0; col < DISPLAY_ICON_SIZE; col++) {
                int map_x = src_x + col;
                uint8_t byte  = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F
                                                  : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;

                for (int dx = 0; dx < DISPLAY_ICON_SCALE; dx++) {
                    int fb_x = dest_x + col * DISPLAY_ICON_SCALE + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

void event_draw_message(GBitmap *fb) {
    if (!s_message_active) return;

    // then render the message centered near the bottom
    GRect fb_bounds = gbitmap_get_bounds(fb);
    int y = fb_bounds.size.h / 2 + 4;  // tune to sit above bottom edge

    if(s_icon_active){
        y = fb_bounds.size.h / 2 + 24 * 2;
    }

    bitfont_render(fb, s_message,
                   fb_bounds.size.w / 2, y,
                   JUSTIFY_CENTER);
}

void event_check_tile(int map_x, int map_y) {
    if (!map_in_bounds(map_x, map_y)) return;
    int8_t event_id = s_event_map[map_y][map_x];
    if (event_id < 0 || event_id >= EVENT_COUNT) return;

    // only fire step events when stepping
    if (s_events[event_id].trigger != EVENT_TRIGGER_STEP) return;
    event_run(event_id);
}

void event_check_forward(int player_x, int player_y, Facing facing) {
    int fx = player_x;
    int fy = player_y;
    switch (facing) {
        case FACING_NORTH: fy--; break;
        case FACING_EAST:  fx++; break;
        case FACING_SOUTH: fy++; break;
        case FACING_WEST:  fx--; break;
    }

    if (!map_in_bounds(fx, fy)) return;
    int8_t event_id = s_event_map[fy][fx];
    if (event_id < 0 || event_id >= EVENT_COUNT) return;

    // only fire forward or item events — not step events
    EventTrigger trigger = s_events[event_id].trigger;
    if (trigger != EVENT_TRIGGER_FORWARD) return;

    event_run(event_id);
}

void event_check_item(int player_x, int player_y, Facing facing, int8_t failed_event) {
    int fx = player_x;
    int fy = player_y;
    switch (facing) {
        case FACING_NORTH: fy--; break;
        case FACING_EAST:  fx++; break;
        case FACING_SOUTH: fy++; break;
        case FACING_WEST:  fx--; break;
    }

    menu_close();

    if(failed_event > -1){
        event_run(failed_event);
        return;
    }

    if (!map_in_bounds(fx, fy)) {
        event_run(s_fail_event);
        return;
    }

    int8_t event_id = s_event_map[fy][fx];
    if (event_id < 0 || event_id >= EVENT_COUNT) {
        event_run(s_fail_event);
        return;
    }

    if (s_events[event_id].trigger != EVENT_TRIGGER_ITEM) {
        event_run(s_fail_event);
        return;
    }
    event_run(event_id);
}

void event_cleanup(void) {
    if (s_message_timer) {
        app_timer_cancel(s_message_timer);
        s_message_timer = NULL;
    }

    if (s_icon_timer) { 
        app_timer_cancel(s_icon_timer);    
        s_icon_timer    = NULL; 
    }

    if (s_shake_timer) {
        app_timer_cancel(s_shake_timer);
        s_shake_timer = NULL;
    }
}

