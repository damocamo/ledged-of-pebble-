#pragma once
#include "pebble.h"
#include "map.h"

// ---- Inventory ----------------------------------------------------------
#define INVENTORY_SLOTS 16  // max distinct item types

typedef struct {
    int8_t quantity;  // 0 = not owned, >0 = count, -1 = unlimited
} InventorySlot;

// ---- Equipment ----------------------------------------------------------
typedef struct {
    const char *name;
    int8_t      bonus_atk;  // added to attack damage
    int8_t      bonus_def;  // damage reduction
    uint8_t     icon_slot;  // icon atlas slot
    int16_t     gold;       // shop price (0 = not sold / starter)
} WeaponDef;

typedef struct {
    const char *name;
    int8_t      bonus_def;
    uint8_t     icon_slot;
    int16_t     gold;
} ArmorDef;

// ---- Spellbook tiers (Signal Magic) -------------------------------------
// 0 = none, 1 = Heal, 2 = Heal+Purge, 3 = Heal+Purge+Decode
typedef enum {
    SPELL_NONE   = 0,
    SPELL_HEAL   = 1,
    SPELL_PURGE  = 2,
    SPELL_DECODE = 3,
} SpellId;

// ---- Player -------------------------------------------------------------
typedef struct {
    int     x;
    int     y;
    int     map_id;
    Facing  facing;

    int8_t  hp;
    int8_t  max_hp;
    int8_t  mp;
    int8_t  max_mp;
    int8_t  def;
    int8_t  dex;
    int8_t  bonus_atk;
    int8_t  bonus_def;
    int16_t gold;
    uint8_t spellbook;

    InventorySlot inventory[INVENTORY_SLOTS];

    int8_t  respawn_map;
    int8_t  respawn_x;
    int8_t  respawn_y;
    Facing  respawn_facing;

    uint8_t weapon;
    uint8_t armor;
} Player;

typedef enum {
    WEAPON_FIST   = 0,
    WEAPON_STICK  = 1,
    WEAPON_DAGGER = 2,
    WEAPON_SPIKE  = 3,
    WEAPON_COUNT
} WeaponId;

typedef enum {
    ARMOR_NONE   = 0,
    ARMOR_CLOAK  = 1,
    ARMOR_VEST   = 2,
    ARMOR_PLATE  = 3,
    ARMOR_SIGNAL = 4,
    ARMOR_COUNT
} ArmorId;

extern Player g_player;

void player_init(void);

void player_give_item(int slot, int quantity);
void player_take_item(int slot, int quantity);
bool player_has_item(int slot);
int  player_item_count(int slot);
int  player_get_mapid(void);

void player_heal(int amount);
void player_restore_mp(int amount);
bool player_spend_mp(int amount);
void player_take_damage(int amount);
bool player_is_dead(void);
bool player_is_hurt(void);
void player_add_gold(int amount);
bool player_spend_gold(int amount);
int  player_get_gold(void);
void player_death_tax(void);

void player_set_spellbook(SpellId id);
void player_add_bonus_atk(int8_t amount);
void player_add_bonus_def(int8_t amount);
void player_add_max_hp(int8_t amount);
void player_add_max_mp(int8_t amount);
const char *player_spell_name(SpellId id);

void player_set_respawn(void);
void player_respawn(void);

void          player_set_weapon(WeaponId id);
void          player_set_armor(ArmorId id);
const WeaponDef *player_get_weapon(void);
const ArmorDef  *player_get_armor(void);
const WeaponDef *player_weapon_by_id(WeaponId id);
const ArmorDef  *player_armor_by_id(ArmorId id);
int8_t        player_weapon_atk(void);
int8_t        player_armor_def(void);
