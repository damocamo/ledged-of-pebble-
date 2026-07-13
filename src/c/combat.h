#pragma once
#include "pebble.h"

#define COMBAT_ENEMY_ICON_SLOT  2  // chest icon as placeholder

// ---- Phases ----
typedef enum {
    COMBAT_PHASE_INTRO   = 0,
    COMBAT_PHASE_INPUT   = 1,
    COMBAT_PHASE_OFFENSE = 2,
    COMBAT_PHASE_DEFENSE = 3,
    COMBAT_PHASE_VICTORY = 4,
    COMBAT_PHASE_DEFEAT  = 5,
    COMBAT_PHASE_IDLE = 6,  // not in combat
} CombatPhase;

// ---- Enemy powers ----
typedef enum {
    ENEMY_POWER_ATTACK  = 0,
    ENEMY_POWER_SCORCH  = 1,  // stronger attack
    ENEMY_POWER_HPDRAIN = 2,  // steals HP
    ENEMY_POWER_MPDRAIN = 3,  // steals MP
    ENEMY_POWER_SHIELD  = 4,  // Signal Shield (boss)
} EnemyPower;

// ---- Enemy categories (for future implementation?) ----
typedef enum {
    ENEMY_CAT_BEAST    = 0,
    ENEMY_CAT_UNDEAD   = 1,
    ENEMY_CAT_DEMON    = 2,
    ENEMY_CAT_AUTOMATON= 3,
} EnemyCategory;

// ---- Enemy definition ----
#define ENEMY_MAX_POWERS 4

typedef struct {
    const char   *name;
    int8_t        hp;
    int8_t        atk_min;
    int8_t        atk_max;
    EnemyCategory category;
    EnemyPower    powers[ENEMY_MAX_POWERS];
    uint8_t       power_count;
    int8_t        gold_min;
    int8_t        gold_max;
    uint8_t       sprite_slot;  // add this
} EnemyDef;

// ---- Enemy IDs ----
typedef enum {
    ENEMY_RAT      = 0,
    ENEMY_BAT      = 1,
    ENEMY_GUARD    = 2,
    ENEMY_WARDEN   = 3,  // boss
    // ---- deeper-floor enemies (difficulty curve) ----
    ENEMY_SKELETON = 4,
    ENEMY_SPIDER   = 5,
    ENEMY_WRAITH   = 6,
    ENEMY_GOLEM    = 7,
    ENEMY_OGRE     = 8,
    ENEMY_LICH     = 9,  // final boss
    ENEMY_COUNT
} EnemyId;

// ---- Combat state ----
#define COMBAT_MSG_LEN 24

typedef struct {
    CombatPhase phase;
    EnemyId     enemy_id;
    int8_t      enemy_hp;

    bool        enemy_hurt;
    bool        hero_hurt;
    bool        run_success;

    int8_t      victory_event;  // event to fire on win, -1 = none
    int8_t      gold_awarded;   // loot granted this fight (-1 = none / escape)
    AppTimer   *phase_timer;
} CombatState;

// ---- API ----
void combat_init(Layer *canvas);
void combat_start(EnemyId enemy_id, int8_t victory_event, bool can_run);
bool combat_is_active(void);
bool combat_can_run(void);

CombatPhase combat_get_phase(void);
void        combat_dismiss(void);

// input — call from click handlers
void combat_input_attack(void);
void combat_input_run(void);
void combat_input_select(void);
void combat_input_item(int item_slot);  // use item in combat
void combat_input_spell(void);          // cast selected Signal spell

// Signal Shield (Keeper / Architect)
bool combat_shield_active(void);
void combat_shield_break(void);
EnemyCategory combat_enemy_category(void);

// draw — call in draw_view before release
void combat_draw(GBitmap *fb);

// At the bottom of combat.h:
int combat_icon_zone_count(void);
int combat_icon_zone_x(int i);
int combat_icon_zone_y(void);
int combat_icon_zone_r(void);