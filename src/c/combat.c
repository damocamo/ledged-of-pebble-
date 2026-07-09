#include "pebble.h"
#include "combat.h"
#include "player.h"
#include "event.h"
#include "bitfont.h"
#include "encounter.h"
#include "save.h"
#include "magic.h"

// ============================================================
// combat.c
// Turn-based RPG combat system
// Adapted from Heroine Dusk combat.js
// https://github.com/clintbellanger/heroine-dusk/blob/master/release/js/combat.js
// ============================================================
//
// COMBAT FLOW:
// 1. Combat starts via CMD_BATTLE event command
// 2. Brief intro phase, then transitions to player input
// 3. Player chooses action: Attack, Run (if allowed), or Use Potion
// 4. Action resolves with hit/miss/crit calculations
// 5. Enemy turn: randomly selects from available powers
// 6. Repeat until victory (enemy HP = 0) or defeat (player HP = 0)
// 7. Victory: runs victory_event, Defeat: respawns player at last checkpoint
//
// COMBAT PHASES:
// - IDLE: No combat active
// - INTRO: Brief pause before player's first turn
// - INPUT: Player selecting action (attack/run/item)
// - OFFENSE: Player action executing
// - DEFENSE: Enemy turn executing
// - VICTORY: Combat won, shows victory message
// - DEFEAT: Player died, shows defeat message
//
// TURN STRUCTURE:
// Each phase uses timer callbacks to create proper pacing:
// - Action appears (e.g., "ATTACK!") → 600ms delay
// - Result appears (e.g., "HIT! -5 HP") → 800ms delay
// - Next phase begins
// This creates a clear rhythm: action → pause → result → pause → next turn
//
// ENEMY POWERS:
// - ATTACK: Standard physical attack
// - SCORCH: Fire damage (higher base damage)
// - HPDRAIN: Damage that heals the enemy
// Each enemy has 1-3 powers and randomly picks one each turn
//
// HIT CALCULATIONS:
// - Player attacks: 20% miss chance, 10% crit chance
//   - Base damage: 1-4 + dexterity bonus + weapon bonus
//   - Crit doubles damage
// - Enemy attacks: 30% miss chance, 5% crit chance
//   - Damage based on enemy's atk_min/atk_max range
//   - SCORCH adds atk_min bonus damage
//   - HPDRAIN restores enemy HP
//
// RUN MECHANIC:
// - 66% success rate
// - Only available if combat's can_run flag is true (bosses disable this)
// - Success: escapes combat, no victory event runs
// - Failure: enemy gets a free turn
//
// VISUAL FEEDBACK:
// - Enemy sprite shakes on taking damage (6 ticks, 3px intensity)
// - World shakes on player taking damage
// - Messages displayed in two-line format:
//   Line 1: Action ("ATTACK!", "CRIT!", "DRAINED!")
//   Line 2: Result ("HIT! -5 HP", "MISS!", "+8 HP")
//
// INPUT COMMANDS:
// - UP button: Attack
// - DOWN button: Use Health Potion (if in inventory)
// - BACK button: Run (if can_run = true)
// - SELECT button: Dismiss victory/defeat screen
//
// BOSS BATTLES:
// Set can_run = false in CMD_BATTLE to prevent running:
// { .type = CMD_BATTLE, .battle = { ENEMY_WARDEN, 60, false } }
//
// DEFEAT HANDLING:
// On defeat, player_respawn() is called, which:
// - Restores HP to max
// - Returns player to last CMD_SET_RESPAWN checkpoint
// - Preserves all map changes and inventory
// - No loss of progress except position reset
//
// ============================================================

// ---- Enemy table ----
static const EnemyDef s_enemies[ENEMY_COUNT] = {
    // ENEMY_RAT
    {
        .name        = "GLITCH RAT",
        .hp          = 6,
        .atk_min     = 1, .atk_max = 4,
        .category    = ENEMY_CAT_BEAST,
        .powers      = { ENEMY_POWER_ATTACK },
        .power_count = 1,
        .gold_min    = 1, .gold_max = 2,
        .sprite_slot = 2,
    },
    // ENEMY_BAT
    {
        .name        = "PIXEL BAT",
        .hp          = 7,
        .atk_min     = 2, .atk_max = 4,
        .category    = ENEMY_CAT_BEAST,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_HPDRAIN },
        .power_count = 2,
        .gold_min    = 1, .gold_max = 3,
        .sprite_slot = 1,
    },
    // ENEMY_GUARD
    {
        .name        = "RIFT GUARD",
        .hp          = 11,
        .atk_min     = 3, .atk_max = 7,
        .category    = ENEMY_CAT_DEMON,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH },
        .power_count = 3,
        .gold_min    = 2, .gold_max = 5,
        .sprite_slot = 0,
    },
    // ENEMY_WARDEN (boss) — L5 mid-boss; shield is one-hit absorb (Purge optional)
    {
        .name        = "THE KEEPER",
        .hp          = 28,
        .atk_min     = 4, .atk_max = 9,
        .category    = ENEMY_CAT_DEMON,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH, ENEMY_POWER_SHIELD },
        .power_count = 3,
        .gold_min    = 20, .gold_max = 30,
        .sprite_slot = 3,
    },
    // ENEMY_SKELETON — floors 3-5
    {
        .name        = "CLOCKBONE",
        .hp          = 15,
        .atk_min     = 4, .atk_max = 8,
        .category    = ENEMY_CAT_UNDEAD,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_ATTACK },
        .power_count = 2,
        .gold_min    = 3, .gold_max = 6,
        .sprite_slot = 4,
    },
    // ENEMY_SPIDER — floors 4-6
    {
        .name        = "GEAR SPIDER",
        .hp          = 17,
        .atk_min     = 4, .atk_max = 9,
        .category    = ENEMY_CAT_BEAST,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH, ENEMY_POWER_ATTACK },
        .power_count = 3,
        .gold_min    = 4, .gold_max = 8,
        .sprite_slot = 5,
    },
    // ENEMY_WRAITH — floors 5-7
    {
        .name        = "STATIC",
        .hp          = 21,
        .atk_min     = 6, .atk_max = 11,
        .category    = ENEMY_CAT_UNDEAD,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_HPDRAIN, ENEMY_POWER_MPDRAIN },
        .power_count = 3,
        .gold_min    = 6, .gold_max = 12,
        .sprite_slot = 6,
    },
    // ENEMY_GOLEM — floors 6-8
    {
        .name        = "QUARTZ GOLEM",
        .hp          = 32,
        .atk_min     = 7, .atk_max = 13,
        .category    = ENEMY_CAT_AUTOMATON,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH },
        .power_count = 2,
        .gold_min    = 8, .gold_max = 15,
        .sprite_slot = 7,
    },
    // ENEMY_OGRE — floors 7-9
    {
        .name        = "RIFT OGRE",
        .hp          = 36,
        .atk_min     = 8, .atk_max = 15,
        .category    = ENEMY_CAT_DEMON,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH, ENEMY_POWER_ATTACK },
        .power_count = 3,
        .gold_min    = 10, .gold_max = 20,
        .sprite_slot = 8,
    },
    // ENEMY_LICH (final boss) — floor 10; tuned for Dagger/Cloak + Purge clears
    {
        .name        = "THE ARCHITECT",
        .hp          = 40,
        .atk_min     = 6, .atk_max = 12,
        .category    = ENEMY_CAT_UNDEAD,
        .powers      = { ENEMY_POWER_ATTACK, ENEMY_POWER_SCORCH, ENEMY_POWER_MPDRAIN, ENEMY_POWER_SHIELD },
        .power_count = 4,
        .gold_min    = 40, .gold_max = 60,
        .sprite_slot = 9,
    },
};

// ---- State ----
static CombatState s_combat;
static Layer      *s_canvas_ref = NULL;

extern GBitmap *s_monsters;
extern GBitmap *s_icon_atlas;

#define ENEMY_SPRITE_W      58
#define ENEMY_SPRITE_H      63
#define ENEMY_SPRITE_SCALE   2
#define ENEMY_DRAW_W        (ENEMY_SPRITE_W * ENEMY_SPRITE_SCALE)
#define ENEMY_DRAW_H        (ENEMY_SPRITE_H * ENEMY_SPRITE_SCALE)

static int  s_enemy_shake_x = 0;
static int  s_enemy_shake_y = 0;
static int  s_enemy_shake_ticks = 0;
static AppTimer *s_enemy_shake_timer = NULL;

// Message display state
static char s_current_action[COMBAT_MSG_LEN] = "";
static char s_current_result[COMBAT_MSG_LEN] = "";
static bool s_show_result = false;

#define CMD_ICON_SIZE   16
#define CMD_ICON_SCALE   3
#define CMD_ICON_DRAW   (CMD_ICON_SIZE * CMD_ICON_SCALE)
#define CMD_ICON_PAD     6
#define CMD_FONT_HEIGHT     24

static int s_icon_zones[4];
static int s_icon_zone_count = 0;
static int s_icon_zone_y     = 0;
static bool s_can_run = true;

// Signal Shield (Keeper / Architect) — absorbs normal attacks; Purge breaks it.
static bool s_shield_active = false;
static int  s_shield_uses   = 0;
#define SHIELD_MAX_USES 3

// DOWN action: 0 = spell (if known), 1 = potion. SELECT cycles when both available.
static int s_down_mode = 0;

// ---- Helpers ----
static int rng_range(int lo, int hi) {
    return lo + (int)(rand() % (hi - lo + 1));
}

static void clear_messages(void) {
    s_current_action[0] = '\0';
    s_current_result[0] = '\0';
    s_show_result = false;
}

// ---- Enemy shake ----
static void enemy_shake_tick(void *context) {
    s_enemy_shake_timer = NULL;
    if (s_enemy_shake_ticks <= 0) {
        s_enemy_shake_x = 0;
        s_enemy_shake_y = 0;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    s_enemy_shake_x = (rand() % 7) - 3;
    s_enemy_shake_y = (rand() % 7) - 3;
    s_enemy_shake_ticks--;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    s_enemy_shake_timer = app_timer_register(80, enemy_shake_tick, NULL);
}

static void start_enemy_shake(void) {
    if (s_enemy_shake_timer) app_timer_cancel(s_enemy_shake_timer);
    s_enemy_shake_ticks = 6;
    s_enemy_shake_timer = app_timer_register(80, enemy_shake_tick, NULL);
}

// Forward declarations
static void phase_tick(void *context);
static void enemy_turn_start(void *context);
static void player_turn_start(void *context);
static void show_action_result(void *context);
static void enemy_perform_action(void *context);
static void enemy_show_result(void *context);
static void attack_show_result(void *context);
static void run_show_result(void *context);
static void item_show_result(void *context);

// ---- Phase transitions ----
static void phase_tick(void *context) {
    s_combat.phase_timer = NULL;
    CombatPhase next = (CombatPhase)(intptr_t)context;
    
    s_combat.phase = next;
    
    if (next == COMBAT_PHASE_INPUT) {
        // Start player turn
        player_turn_start(NULL);
    } else if (next == COMBAT_PHASE_DEFENSE) {
        // Start enemy turn
        enemy_turn_start(NULL);
    } else if (next == COMBAT_PHASE_VICTORY) {
        int8_t ve = s_combat.victory_event;
        combat_dismiss();
        if (ve >= 0) {
            event_run(ve);
        }
        save_write();  // after victory flags / gold
    } else if (next == COMBAT_PHASE_DEFEAT) {
        player_death_tax();
        player_respawn();
        combat_dismiss();
        save_write();
    }
    
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

static void player_turn_start(void *context) {
    clear_messages();
    snprintf(s_current_action, sizeof(s_current_action), "YOUR TURN");
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

// Show result after action delay
static void show_action_result(void *context) {
    s_combat.phase_timer = NULL;
    s_show_result = true;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    // Check if enemy died
    if (s_combat.enemy_hp <= 0) {
        const EnemyDef *def = &s_enemies[s_combat.enemy_id];
        int gold = 0;
        // Scripted fights (victory_event >= 0) always pay; random fights are capped.
        bool scripted = (s_combat.victory_event >= 0);
        if (scripted || encounter_can_award_gold(s_combat.enemy_id)) {
            gold = rng_range(def->gold_min, def->gold_max);
            player_add_gold(gold);
            if (!scripted) {
                encounter_note_gold_kill(s_combat.enemy_id);
            }
        }
        s_combat.gold_awarded = (int8_t)gold;
        s_combat.phase = COMBAT_PHASE_VICTORY;
        s_combat.phase_timer = app_timer_register(1500, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_VICTORY);
        return;
    }
    
    // Move to enemy turn
    s_combat.phase = COMBAT_PHASE_DEFENSE;
    s_combat.phase_timer = app_timer_register(1200, phase_tick,
        (void*)(intptr_t)COMBAT_PHASE_DEFENSE);
}

static void enemy_turn_start(void *context) {
    s_combat.phase_timer = NULL;
    clear_messages();
    
    //const EnemyDef *def = &s_enemies[s_combat.enemy_id];
    snprintf(s_current_action, sizeof(s_current_action), "ENEMY TURN");
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    // Delay before showing enemy action
    s_combat.phase_timer = app_timer_register(800, enemy_perform_action, NULL);
}

static void enemy_perform_action(void *context) {
    s_combat.phase_timer = NULL;
    
    const EnemyDef *def = &s_enemies[s_combat.enemy_id];
    EnemyPower power = def->powers[rng_range(0, def->power_count-1)];
    //int miss_roll = rng_range(0, 99);
    
    // Set action name
    switch (power) {
        case ENEMY_POWER_ATTACK:
            snprintf(s_current_action, sizeof(s_current_action), "ATTACKS!");
            break;
        case ENEMY_POWER_SCORCH:
            snprintf(s_current_action, sizeof(s_current_action), "SCORCHES!");
            break;
        case ENEMY_POWER_HPDRAIN:
            snprintf(s_current_action, sizeof(s_current_action), "DRAINS!");
            break;
        case ENEMY_POWER_MPDRAIN:
            snprintf(s_current_action, sizeof(s_current_action), "MP DRAIN!");
            break;
        case ENEMY_POWER_SHIELD:
            snprintf(s_current_action, sizeof(s_current_action), "SHIELD!");
            break;
    }
    
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    // Wait before showing result
    s_combat.phase_timer = app_timer_register(600, enemy_show_result, (void*)(intptr_t)power);
}

static void enemy_show_result(void *context) {
    s_combat.phase_timer = NULL;
    
    EnemyPower power = (EnemyPower)(intptr_t)context;
    const EnemyDef *def = &s_enemies[s_combat.enemy_id];
    int miss_roll = rng_range(0, 99);
    
    if (miss_roll < 30) {
        snprintf(s_current_result, sizeof(s_current_result), "MISS!");
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        
        // Back to player turn
        s_combat.phase_timer = app_timer_register(1200, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_INPUT);
        return;
    }
    
    int dmg = 0;
    bool crit = (rng_range(0,99) < 5);

    if (power == ENEMY_POWER_SHIELD) {
        if (!s_shield_active && s_shield_uses < SHIELD_MAX_USES) {
            s_shield_active = true;
            s_shield_uses++;
            snprintf(s_current_action, sizeof(s_current_action), "SIGNAL!");
            snprintf(s_current_result, sizeof(s_current_result), "+DEF UP");
        } else {
            // Already shielded or out of uses — fall back to attack
            dmg = rng_range(def->atk_min, def->atk_max);
            player_take_damage(dmg);
            event_start_shake(6, 3);
            snprintf(s_current_action, sizeof(s_current_action), "HIT!");
            snprintf(s_current_result, sizeof(s_current_result), "-%d HP", dmg);
        }
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        if (player_is_dead()) {
            s_combat.phase = COMBAT_PHASE_DEFEAT;
            s_combat.phase_timer = app_timer_register(2000, phase_tick,
                (void*)(intptr_t)COMBAT_PHASE_DEFEAT);
            return;
        }
        s_combat.phase_timer = app_timer_register(1200, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_INPUT);
        return;
    }

    if (power == ENEMY_POWER_MPDRAIN) {
        if (g_player.mp > 0) {
            g_player.mp--;
            snprintf(s_current_action, sizeof(s_current_action), "DRAINED!");
            snprintf(s_current_result, sizeof(s_current_result), "-1 MP");
        } else {
            snprintf(s_current_action, sizeof(s_current_action), "DRAIN!");
            snprintf(s_current_result, sizeof(s_current_result), "NO EFFECT");
        }
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        s_combat.phase_timer = app_timer_register(1200, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_INPUT);
        return;
    }
    
    switch (power) {
        case ENEMY_POWER_ATTACK:
            dmg = rng_range(def->atk_min, def->atk_max);
            if (crit) dmg *= 2;
            break;
        case ENEMY_POWER_SCORCH:
            dmg = rng_range(def->atk_min, def->atk_max) + def->atk_min;
            break;
        case ENEMY_POWER_HPDRAIN:
            dmg = rng_range(def->atk_min, def->atk_max);
            s_combat.enemy_hp += dmg;
            if (s_combat.enemy_hp > def->hp) s_combat.enemy_hp = def->hp;
            break;
        default:
            dmg = rng_range(def->atk_min, def->atk_max);
            break;
    }
    
    player_take_damage(dmg);
    event_start_shake(6, 3);
    
    if (crit) {
        snprintf(s_current_action, sizeof(s_current_action), "CRIT!");
        snprintf(s_current_result, sizeof(s_current_result), "-%d HP", dmg);
    } else if (power == ENEMY_POWER_HPDRAIN) {
        snprintf(s_current_action, sizeof(s_current_action), "DRAINED!");
        snprintf(s_current_result, sizeof(s_current_result), "%d HP", dmg);
    } else {
        snprintf(s_current_action, sizeof(s_current_action), "HIT!");
        snprintf(s_current_result, sizeof(s_current_result), "-%d HP", dmg);
    }
    
    s_show_result = true;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    // Check if player died
    if (player_is_dead()) {
        s_combat.phase = COMBAT_PHASE_DEFEAT;
        s_combat.phase_timer = app_timer_register(2000, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_DEFEAT);
        return;
    }
    
    // Back to player turn
    s_combat.phase_timer = app_timer_register(1200, phase_tick,
        (void*)(intptr_t)COMBAT_PHASE_INPUT);
}

// ---- Public API ----
void combat_init(Layer *canvas) {
    s_canvas_ref = canvas;
    s_combat.phase = COMBAT_PHASE_IDLE;
    s_combat.enemy_hp = 0;
    s_combat.phase_timer = NULL;
}

void combat_start(EnemyId enemy_id, int8_t victory_event, bool can_run) {
    s_combat.enemy_id      = enemy_id;
    s_combat.enemy_hp      = s_enemies[enemy_id].hp;
    s_combat.victory_event = victory_event;
    s_combat.gold_awarded  = -1;
    s_combat.phase         = COMBAT_PHASE_INTRO;
    s_can_run              = can_run;
    s_shield_active        = false;
    s_shield_uses          = 0;
    s_down_mode            = 0;
    magic_reset_selection();

    clear_messages();
    
    if (s_combat.phase_timer) {
        app_timer_cancel(s_combat.phase_timer);
    }
    s_combat.phase_timer = app_timer_register(800, phase_tick,
        (void*)(intptr_t)COMBAT_PHASE_INPUT);

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

bool combat_is_active(void) {
    return s_combat.phase != COMBAT_PHASE_IDLE;
}

CombatPhase combat_get_phase(void) {
    return s_combat.phase;
}

void combat_dismiss(void) {
    if (s_combat.phase_timer) {
        app_timer_cancel(s_combat.phase_timer);
        s_combat.phase_timer = NULL;
    }
    clear_messages();
    s_combat.phase    = COMBAT_PHASE_IDLE;
    s_combat.enemy_hp = 0;
    s_enemy_shake_x   = 0;
    s_enemy_shake_y   = 0;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

// ---- Player input ----
void combat_input_attack(void) {
    if (s_combat.phase != COMBAT_PHASE_INPUT) return;
    
    clear_messages();
    snprintf(s_current_action, sizeof(s_current_action), "ATTACK!");
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    s_combat.phase = COMBAT_PHASE_OFFENSE;
    
    // Wait before showing result
    s_combat.phase_timer = app_timer_register(600, attack_show_result, NULL);
}

static void attack_show_result(void *context) {
    s_combat.phase_timer = NULL;

    if (s_shield_active) {
        // One-hit absorb, then shield drops (Purge still breaks it early).
        s_shield_active = false;
        snprintf(s_current_action, sizeof(s_current_action), "ABSORBED!");
        snprintf(s_current_result, sizeof(s_current_result), "SHIELD DOWN");
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
        return;
    }
    
    int miss_roll = rng_range(0, 99);
    if (miss_roll < 20) {
        snprintf(s_current_result, sizeof(s_current_result), "MISS!");
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        
        // Still move to enemy turn
        s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
        return;
    }
    
    int atk_min = 1 + g_player.dex / 3 + player_weapon_atk();
    int atk_max = 4 + g_player.dex / 2 + player_weapon_atk();
    int dmg     = rng_range(atk_min, atk_max);
    bool crit   = (rng_range(0,99) < 10);
    
    if (crit) {
        dmg += atk_max;

        snprintf(s_current_action, sizeof(s_current_action), "CRIT!");
        snprintf(s_current_result, sizeof(s_current_result), "-%d HP", dmg);
    } else {

        snprintf(s_current_action, sizeof(s_current_action), "HIT!");
        snprintf(s_current_result, sizeof(s_current_result), "-%d HP", dmg);
    }
    
    s_combat.enemy_hp -= dmg;
    if (s_combat.enemy_hp < 0) s_combat.enemy_hp = 0;
    
    start_enemy_shake();
    s_show_result = true;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    // Delay before next phase
    s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
}

void combat_input_select(void) {
    // During input: cycle DOWN action (spell ↔ potion) or spell selection
    if (s_combat.phase == COMBAT_PHASE_INPUT) {
        bool has_spell = g_player.spellbook >= SPELL_HEAL;
        bool has_potion = player_has_item(0);
        if (has_spell && has_potion) {
            s_down_mode ^= 1;
            if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
            return;
        }
        if (has_spell) {
            magic_cycle_spell();
            if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
            return;
        }
    }
    // Early dismiss: cancel the auto-advance timer so victory/defeat
    // handling runs exactly once (not again from phase_tick).
    if (s_combat.phase == COMBAT_PHASE_VICTORY) {
        if (s_combat.phase_timer) {
            app_timer_cancel(s_combat.phase_timer);
            s_combat.phase_timer = NULL;
        }
        int8_t ve = s_combat.victory_event;
        combat_dismiss();
        if (ve >= 0) {
            event_run(ve);
        }
        save_write();
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    } else if (s_combat.phase == COMBAT_PHASE_DEFEAT) {
        if (s_combat.phase_timer) {
            app_timer_cancel(s_combat.phase_timer);
            s_combat.phase_timer = NULL;
        }
        player_death_tax();
        player_respawn();
        combat_dismiss();
        save_write();
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    }
}

void combat_input_run(void) {
    if (!s_can_run) return;
    if (s_combat.phase != COMBAT_PHASE_INPUT) return;
    
    clear_messages();
    snprintf(s_current_action, sizeof(s_current_action), "RUN!");
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    s_combat.phase = COMBAT_PHASE_OFFENSE;
    s_combat.phase_timer = app_timer_register(600, run_show_result, NULL);
}

static void run_show_result(void *context) {
    s_combat.phase_timer = NULL;
    
    bool success = (rng_range(0,99) < 66);
    
    if (success) {
        snprintf(s_current_result, sizeof(s_current_result), "ESCAPED!");
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        
        s_combat.victory_event = -2;
        s_combat.phase = COMBAT_PHASE_VICTORY;
        s_combat.phase_timer = app_timer_register(1200, phase_tick,
            (void*)(intptr_t)COMBAT_PHASE_VICTORY);
    } else {
        snprintf(s_current_result, sizeof(s_current_result), "FAILED!");
        s_show_result = true;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        
        s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
    }
}

void combat_input_item(int item_slot) {
    if (s_combat.phase != COMBAT_PHASE_INPUT) return;
    if (!player_has_item(item_slot)) return;
    
    clear_messages();
    snprintf(s_current_action, sizeof(s_current_action), "USE POTION!");
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    s_combat.phase = COMBAT_PHASE_OFFENSE;
    s_combat.phase_timer = app_timer_register(600, item_show_result, NULL);
}

static void item_show_result(void *context) {
    s_combat.phase_timer = NULL;
    
    int heal = rng_range(g_player.max_hp/4, g_player.max_hp/2);
    player_heal(heal);
    player_take_item(0, 1);
    
    snprintf(s_current_result, sizeof(s_current_result), "+%d HP", heal);
    s_show_result = true;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    
    s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
}

static void spell_show_result(void *context) {
    s_combat.phase_timer = NULL;
    SpellId sp = (SpellId)(intptr_t)context;
    int8_t dmg = 0;
    bool ok = false;

    switch (sp) {
        case SPELL_HEAL:
            ok = magic_combat_heal();
            break;
        case SPELL_PURGE:
            ok = magic_combat_purge(&dmg);
            if (ok && dmg > 0) {
                s_combat.enemy_hp -= dmg;
                if (s_combat.enemy_hp < 0) s_combat.enemy_hp = 0;
                start_enemy_shake();
            }
            break;
        case SPELL_DECODE:
            ok = magic_combat_decode(&dmg);
            if (ok && dmg > 0) {
                s_combat.enemy_hp -= dmg;
                if (s_combat.enemy_hp < 0) s_combat.enemy_hp = 0;
                start_enemy_shake();
            }
            break;
        default:
            break;
    }

    if (!ok) {
        snprintf(s_current_action, sizeof(s_current_action), "FAILED!");
    }
    snprintf(s_current_result, sizeof(s_current_result), "%.23s", g_magic_msg);
    s_show_result = true;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);

    // Failed cast (no MP / no spell) still spends the turn.
    s_combat.phase_timer = app_timer_register(800, show_action_result, NULL);
}

void combat_input_spell(void) {
    if (s_combat.phase != COMBAT_PHASE_INPUT) return;

    bool has_spell = g_player.spellbook >= SPELL_HEAL;
    bool has_potion = player_has_item(0);

    // Prefer potion when down_mode says so, or when no spells / no MP for heal-only
    if (has_potion && (!has_spell || s_down_mode == 1)) {
        combat_input_item(0);
        return;
    }
    if (!has_spell) {
        if (has_potion) combat_input_item(0);
        return;
    }

    SpellId sp = magic_selected_spell();
    clear_messages();
    snprintf(s_current_action, sizeof(s_current_action), "%s!",
             player_spell_name(sp));
    s_show_result = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);

    s_combat.phase = COMBAT_PHASE_OFFENSE;
    s_combat.phase_timer = app_timer_register(600, spell_show_result,
        (void*)(intptr_t)sp);
}

bool combat_shield_active(void) {
    return s_shield_active;
}

void combat_shield_break(void) {
    s_shield_active = false;
}

EnemyCategory combat_enemy_category(void) {
    if (s_combat.phase == COMBAT_PHASE_IDLE) return ENEMY_CAT_BEAST;
    return s_enemies[s_combat.enemy_id].category;
}

// ---- Draw helpers ----
static void draw_enemy_sprite(GBitmap *fb, int dest_x, int dest_y) {
    if (!s_monsters) return;

    const EnemyDef *def = &s_enemies[s_combat.enemy_id];
    int sprite_x = def->sprite_slot * ENEMY_SPRITE_W;

    uint8_t *data    = gbitmap_get_data(s_monsters);
    int      stride  = gbitmap_get_bytes_per_row(s_monsters);
    GColor  *palette = gbitmap_get_palette(s_monsters);
    GRect    fb_bounds = gbitmap_get_bounds(fb);

    dest_x += s_enemy_shake_x;
    dest_y += s_enemy_shake_y;

    for (int row = 0; row < ENEMY_SPRITE_H; row++) {
        for (int dy = 0; dy < ENEMY_SPRITE_SCALE; dy++) {
            int fb_y = dest_y + row * ENEMY_SPRITE_SCALE + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);

            for (int col = 0; col < ENEMY_SPRITE_W; col++) {
                int map_x = sprite_x + col;
                uint8_t byte  = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;

                for (int dx = 0; dx < ENEMY_SPRITE_SCALE; dx++) {
                    int fb_x = dest_x + col * ENEMY_SPRITE_SCALE + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

static void draw_combat_icon(GBitmap *fb, int icon_slot, int dest_x, int dest_y) {
    if (!s_icon_atlas) return;

    uint8_t *data    = gbitmap_get_data(s_icon_atlas);
    int      stride  = gbitmap_get_bytes_per_row(s_icon_atlas);
    GColor  *palette = gbitmap_get_palette(s_icon_atlas);
    GRect    fb_bounds = gbitmap_get_bounds(fb);

    int src_x = icon_slot * CMD_ICON_SIZE;

    for (int row = 0; row < CMD_ICON_SIZE; row++) {
        for (int dy = 0; dy < CMD_ICON_SCALE; dy++) {
            int fb_y = dest_y + row * CMD_ICON_SCALE + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);

            for (int col = 0; col < CMD_ICON_SIZE; col++) {
                int map_x = src_x + col;
                uint8_t byte  = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;

                for (int dx = 0; dx < CMD_ICON_SCALE; dx++) {
                    int fb_x = dest_x + col * CMD_ICON_SCALE + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

#define ICON_SLOT_ATTACK  0
#define ICON_SLOT_RUN     1
#define ICON_SLOT_POTION  9
#define ICON_SLOT_SPELL  12

static void draw_combat_commands(GBitmap *fb, int cy, int screen_w, bool has_potion, bool has_spell) {
    // Normalize down_mode
    if (!has_spell) s_down_mode = 1;
    else if (!has_potion) s_down_mode = 0;

    int icon_count = 1; // attack
    if (has_spell || has_potion) icon_count++;
    if (s_can_run) icon_count++;

    int total_w = icon_count * CMD_ICON_DRAW + (icon_count - 1) * CMD_ICON_PAD;
    int start_x = screen_w / 2 - total_w / 2;
    int icon_y  = cy + 114 + CMD_ICON_PAD*2 - CMD_FONT_HEIGHT - CMD_ICON_DRAW;

    s_icon_zone_count = 0;
    s_icon_zone_y     = icon_y + CMD_ICON_DRAW / 2;

    int slots[4];
    int count = 0;
    slots[count++] = ICON_SLOT_ATTACK;
    if (has_spell && has_potion) {
        slots[count++] = (s_down_mode == 1) ? ICON_SLOT_POTION : ICON_SLOT_SPELL;
    } else if (has_spell) {
        slots[count++] = ICON_SLOT_SPELL;
    } else if (has_potion) {
        slots[count++] = ICON_SLOT_POTION;
    }
    if (s_can_run)  slots[count++] = ICON_SLOT_RUN;

    for (int i = 0; i < count; i++) {
        int ix = start_x + i * (CMD_ICON_DRAW + CMD_ICON_PAD);
        s_icon_zones[s_icon_zone_count++] = ix + CMD_ICON_DRAW / 2;
        
        draw_combat_icon(fb, slots[i], ix, icon_y);

        const char *label = NULL;
        if (slots[i] == ICON_SLOT_ATTACK) label = "UP";
        if (slots[i] == ICON_SLOT_POTION || slots[i] == ICON_SLOT_SPELL) label = "DN";
        if (slots[i] == ICON_SLOT_RUN)    label = "BK";

        if (label) {
            bitfont_render(fb, label, ix + CMD_ICON_DRAW / 2,
                           icon_y + CMD_ICON_DRAW - CMD_ICON_PAD*2 + 3,
                           JUSTIFY_CENTERV);
        }
    }
}

int combat_icon_zone_count(void) { return s_icon_zone_count; }
int combat_icon_zone_x(int i)    { return s_icon_zones[i]; }
int combat_icon_zone_y(void)     { return s_icon_zone_y; }
int combat_icon_zone_r(void)     { return CMD_ICON_DRAW / 2 + 8; }

// ---- Draw ----
void combat_draw(GBitmap *fb) {
    if (!combat_is_active()) return;

    GRect fb_bounds = gbitmap_get_bounds(fb);
    int cx = fb_bounds.size.w / 2;
    int cy = fb_bounds.size.h / 2;

    const EnemyDef *def = &s_enemies[s_combat.enemy_id];

    // Victory/Escape/Defeat
    if (s_combat.phase == COMBAT_PHASE_VICTORY) {
        bitfont_render(fb, s_combat.victory_event < -1 ? "ESCAPED!" : "VICTORY!",
                       cx, cy - CMD_FONT_HEIGHT, JUSTIFY_CENTER);
        if (s_combat.gold_awarded > 0) {
            static char gold_msg[COMBAT_MSG_LEN];
            snprintf(gold_msg, sizeof(gold_msg), "+%d GOLD", s_combat.gold_awarded);
            bitfont_render(fb, gold_msg, cx, cy, JUSTIFY_CENTER);
            bitfont_render(fb, "SL: CONTINUE", cx, cy + CMD_FONT_HEIGHT, JUSTIFY_CENTER);
        } else {
            bitfont_render(fb, "SL: CONTINUE", cx, cy + CMD_FONT_HEIGHT, JUSTIFY_CENTER);
        }
        return;
    }

    if (s_combat.phase == COMBAT_PHASE_DEFEAT) {
        bitfont_render(fb, "DEFEATED...", cx, cy, JUSTIFY_CENTER);
        bitfont_render(fb, "SL: CONTINUE", cx, cy + CMD_FONT_HEIGHT*2, JUSTIFY_CENTER);
        return;
    }

    // Enemy Sprite Draw
    int sprite_x = (cx/2*3) - (ENEMY_DRAW_W / 2);
    int sprite_y = cy - ENEMY_DRAW_H / 2;
    draw_enemy_sprite(fb, sprite_x, sprite_y);

    // Enemy name (+ shield pip)
    if (s_shield_active) {
        bitfont_render(fb, "SHIELDED", cx, cy - 114 + CMD_FONT_HEIGHT, JUSTIFY_CENTER);
    } else {
        bitfont_render(fb, def->name, cx, cy - 114 + CMD_FONT_HEIGHT, JUSTIFY_CENTER);
    }
    
    // Current action/result
    int rx = cx-90; 
    int ry = cy-114+CMD_FONT_HEIGHT;
    if (s_current_action[0]) {
        bitfont_render(fb, s_current_action, rx, ry+CMD_FONT_HEIGHT*2+CMD_ICON_SCALE*4, JUSTIFY_LEFT);
    }
    if (s_show_result && s_current_result[0]) {
        bitfont_render(fb, s_current_result, rx, ry+CMD_FONT_HEIGHT*3+CMD_ICON_SCALE*5, JUSTIFY_LEFT);
    }

    // Player HP / MP
    static char php_buf[20];
    snprintf(php_buf, sizeof(php_buf), "HP %d/%d", g_player.hp, g_player.max_hp);
    bitfont_render(fb, php_buf, cx - 90,
                   cy + 114 + CMD_ICON_PAD*2 - CMD_FONT_HEIGHT*2 - CMD_ICON_DRAW - CMD_ICON_SCALE,
                   JUSTIFY_LEFT);
    snprintf(php_buf, sizeof(php_buf), "MP %d/%d", g_player.mp, g_player.max_mp);
    bitfont_render(fb, php_buf, cx + 90,
                   cy + 114 + CMD_ICON_PAD*2 - CMD_FONT_HEIGHT*2 - CMD_ICON_DRAW - CMD_ICON_SCALE,
                   JUSTIFY_RIGHT);

    // Controls — SELECT cycles spell↔potion when both available
    if (s_combat.phase == COMBAT_PHASE_INPUT) {
        bool has_spell = g_player.spellbook >= SPELL_HEAL;
        bool has_potion = player_has_item(0);
        draw_combat_commands(fb, cy, fb_bounds.size.w, has_potion, has_spell);
        if (has_spell && s_down_mode == 0) {
            bitfont_render(fb, player_spell_name(magic_selected_spell()),
                           cx, cy + 114 - CMD_FONT_HEIGHT - CMD_ICON_DRAW - 18,
                           JUSTIFY_CENTER);
        } else if (has_potion && (s_down_mode == 1 || !has_spell)) {
            bitfont_render(fb, "POTION",
                           cx, cy + 114 - CMD_FONT_HEIGHT - CMD_ICON_DRAW - 18,
                           JUSTIFY_CENTER);
        }
    }
}