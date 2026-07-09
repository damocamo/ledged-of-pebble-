#include "pebble.h"
#include "player.h"
#include "map1.h"
#include "save.h"
#include "event.h"
#include "encounter.h"
#include "puzzle.h"

Player g_player;

void player_init(void) {
    g_player = (Player){
        .x       = MAP1_START_X,
        .y       = MAP1_START_Y,
        .map_id  = 0,
        .facing  = MAP1_START_FACING,
        .hp      = 25,
        .max_hp  = 25,
        .mp      = 4,
        .max_mp  = 4,
        .def     = 0,
        .dex     = 5,
        .bonus_atk = 0,
        .bonus_def = 0,
        .gold    = 0,
        .spellbook = SPELL_NONE,
        .weapon  = WEAPON_FIST,
        .armor   = ARMOR_NONE,
        .respawn_x = MAP1_START_X,
        .respawn_y = MAP1_START_Y,
        .respawn_facing = MAP1_START_FACING,
    };

    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        g_player.inventory[i].quantity = 0;
    }
}

static const WeaponDef s_weapons[WEAPON_COUNT] = {
    { "FIST",         0, 0,  0,  0 },
    { "STICK",        2, 0, 14,  0 },
    { "DAGGER",       4, 0, 18, 25 },
    { "SIGNAL BLADE", 6, 0, 18, 55 },
};

static const ArmorDef s_armor[ARMOR_COUNT] = {
    { "NONE",        0,  0,  0 },
    { "CLOAK",       2, 15, 18 },
    { "VEST",        4, 15, 30 },
    { "PLATE",       6, 15, 45 },
    { "SIGNAL MAIL", 8, 15, 65 },
};

void player_set_weapon(WeaponId id) {
    if (id < WEAPON_COUNT && g_player.weapon < (uint8_t)id)
        g_player.weapon = (uint8_t)id;
}

void player_set_armor(ArmorId id) {
    if (id < ARMOR_COUNT && g_player.armor < (uint8_t)id)
        g_player.armor = (uint8_t)id;
}

const WeaponDef *player_get_weapon(void) {
    return &s_weapons[(int)g_player.weapon];
}

const ArmorDef *player_get_armor(void) {
    return &s_armor[(int)g_player.armor];
}

const WeaponDef *player_weapon_by_id(WeaponId id) {
    if (id >= WEAPON_COUNT) id = WEAPON_FIST;
    return &s_weapons[(int)id];
}

const ArmorDef *player_armor_by_id(ArmorId id) {
    if (id >= ARMOR_COUNT) id = ARMOR_NONE;
    return &s_armor[(int)id];
}

int8_t player_weapon_atk(void) {
    return (int8_t)(s_weapons[(int)g_player.weapon].bonus_atk + g_player.bonus_atk);
}

int8_t player_armor_def(void) {
    return (int8_t)(g_player.def + g_player.bonus_def
                    + s_armor[(int)g_player.armor].bonus_def);
}

void player_give_item(int slot, int quantity) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    g_player.inventory[slot].quantity += quantity;
    if (g_player.inventory[slot].quantity > 99)
        g_player.inventory[slot].quantity = 99;
}

void player_take_item(int slot, int quantity) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    g_player.inventory[slot].quantity -= quantity;
    if (g_player.inventory[slot].quantity < 0)
        g_player.inventory[slot].quantity = 0;
}

bool player_has_item(int slot) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return false;
    return g_player.inventory[slot].quantity > 0;
}

int player_item_count(int slot) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return 0;
    return g_player.inventory[slot].quantity;
}

int player_get_mapid(void) {
    return g_player.map_id;
}

void player_heal(int amount) {
    g_player.hp += amount;
    if (g_player.hp > g_player.max_hp)
        g_player.hp = g_player.max_hp;
}

void player_restore_mp(int amount) {
    if (amount < 0) {
        g_player.mp = g_player.max_mp;
        return;
    }
    g_player.mp += amount;
    if (g_player.mp > g_player.max_mp)
        g_player.mp = g_player.max_mp;
}

bool player_spend_mp(int amount) {
    if (amount <= 0) return true;
    if (g_player.mp < amount) return false;
    g_player.mp = (int8_t)(g_player.mp - amount);
    return true;
}

void player_take_damage(int amount) {
    int reduced = amount - player_armor_def();
    if (reduced < 1) reduced = 1;
    g_player.hp -= reduced;
    if (g_player.hp < 0) g_player.hp = 0;
}

bool player_is_dead(void) {
    return g_player.hp <= 0;
}

bool player_is_hurt(void) {
    return g_player.hp <= g_player.max_hp / 3;
}

void player_add_gold(int amount) {
    if (amount <= 0) return;
    int total = (int)g_player.gold + amount;
    if (total > 9999) total = 9999;
    g_player.gold = (int16_t)total;
}

bool player_spend_gold(int amount) {
    if (amount <= 0) return true;
    if (g_player.gold < amount) return false;
    g_player.gold = (int16_t)(g_player.gold - amount);
    return true;
}

int player_get_gold(void) {
    return g_player.gold;
}

void player_death_tax(void) {
    g_player.gold = (int16_t)(g_player.gold / 2);
}

void player_set_spellbook(SpellId id) {
    if ((uint8_t)id > g_player.spellbook)
        g_player.spellbook = (uint8_t)id;
}

void player_add_bonus_atk(int8_t amount) {
    int v = (int)g_player.bonus_atk + amount;
    if (v > 99) v = 99;
    if (v < 0) v = 0;
    g_player.bonus_atk = (int8_t)v;
}

void player_add_bonus_def(int8_t amount) {
    int v = (int)g_player.bonus_def + amount;
    if (v > 99) v = 99;
    if (v < 0) v = 0;
    g_player.bonus_def = (int8_t)v;
}

void player_add_max_hp(int8_t amount) {
    int mh = (int)g_player.max_hp + amount;
    if (mh > 99) mh = 99;
    if (mh < 1) mh = 1;
    g_player.max_hp = (int8_t)mh;
    player_heal(amount > 0 ? amount : 0);
}

void player_add_max_mp(int8_t amount) {
    int mm = (int)g_player.max_mp + amount;
    if (mm > 99) mm = 99;
    if (mm < 0) mm = 0;
    g_player.max_mp = (int8_t)mm;
    player_restore_mp(amount > 0 ? amount : 0);
}

const char *player_spell_name(SpellId id) {
    switch (id) {
        case SPELL_HEAL:   return "HEAL";
        case SPELL_PURGE:  return "PURGE";
        case SPELL_DECODE: return "DECODE";
        default:           return "NONE";
    }
}

void player_set_respawn(void) {
    g_player.respawn_map = g_player.map_id;
    g_player.respawn_x   = g_player.x;
    g_player.respawn_y   = g_player.y;
    g_player.respawn_facing = g_player.facing;
}

void player_respawn(void) {
    g_player.hp = g_player.max_hp;
    g_player.mp = g_player.max_mp;

    if (g_player.respawn_map != g_player.map_id) {
        save_flush_map_deltas(g_player.map_id);
        g_player.map_id = g_player.respawn_map;
        map_init_for(g_player.map_id);
        event_load_map(g_player.map_id);
        encounter_init(g_player.map_id);
        save_load_map_deltas(g_player.map_id);
    }

    g_player.x = g_player.respawn_x;
    g_player.y = g_player.respawn_y;
    g_player.facing = g_player.respawn_facing;

    puzzle_recompute_plates(g_player.x, g_player.y);
}
