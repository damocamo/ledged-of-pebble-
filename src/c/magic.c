#include "pebble.h"
#include "magic.h"
#include "player.h"
#include "map.h"
#include "combat.h"
#include "save.h"

char g_magic_msg[28] = "";

static SpellId s_selected = SPELL_HEAL;

void magic_reset_selection(void) {
    if (g_player.spellbook >= SPELL_HEAL)
        s_selected = SPELL_HEAL;
    else
        s_selected = SPELL_NONE;
}

SpellId magic_selected_spell(void) {
    if (g_player.spellbook == SPELL_NONE) return SPELL_NONE;
    if (s_selected < SPELL_HEAL || s_selected > g_player.spellbook)
        s_selected = SPELL_HEAL;
    return s_selected;
}

void magic_cycle_spell(void) {
    if (g_player.spellbook < SPELL_HEAL) {
        s_selected = SPELL_NONE;
        return;
    }
    s_selected++;
    if (s_selected > g_player.spellbook)
        s_selected = SPELL_HEAL;
}

static int facing_dx(void) {
    switch (g_player.facing) {
        case FACING_EAST:  return 1;
        case FACING_WEST:  return -1;
        default: return 0;
    }
}

static int facing_dy(void) {
    switch (g_player.facing) {
        case FACING_SOUTH: return 1;
        case FACING_NORTH: return -1;
        default: return 0;
    }
}

bool magic_combat_heal(void) {
    if (g_player.spellbook < SPELL_HEAL) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO SPELL");
        return false;
    }
    if (g_player.mp < 1) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO MP");
        return false;
    }
    if (g_player.hp >= g_player.max_hp) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "FULL HP");
        return false;
    }
    player_spend_mp(1);
    int heal = g_player.max_hp / 2 + (rand() % (g_player.max_hp / 2 + 1));
    if (heal < 1) heal = 1;
    player_heal(heal);
    snprintf(g_magic_msg, sizeof(g_magic_msg), "+%d HP", heal);
    return true;
}

bool magic_combat_purge(int8_t *out_damage) {
    if (g_player.spellbook < SPELL_PURGE) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO SPELL");
        return false;
    }
    if (g_player.mp < 1) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO MP");
        return false;
    }
    player_spend_mp(1);

    // Break Keeper / Architect signal shield if active.
    if (combat_shield_active()) {
        combat_shield_break();
        if (out_damage) *out_damage = 0;
        snprintf(g_magic_msg, sizeof(g_magic_msg), "SHIELD DOWN");
        return true;
    }

    int atk_min = 1 + g_player.dex / 3 + player_weapon_atk();
    int atk_max = 4 + g_player.dex / 2 + player_weapon_atk();
    int dmg = atk_min + (rand() % (atk_max - atk_min + 1));

    EnemyCategory cat = combat_enemy_category();
    if (cat == ENEMY_CAT_UNDEAD) {
        dmg += atk_max + atk_max;  // 2x crit vs undead
    } else if (cat != ENEMY_CAT_DEMON) {
        dmg += atk_max;            // 1x crit vs most
    }
    // vs demon: weapon damage only

    if (out_damage) *out_damage = (int8_t)dmg;
    snprintf(g_magic_msg, sizeof(g_magic_msg), "-%d HP", dmg);
    return true;
}

bool magic_combat_decode(int8_t *out_damage) {
    if (g_player.spellbook < SPELL_DECODE) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO SPELL");
        return false;
    }
    if (g_player.mp < 1) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO MP");
        return false;
    }
    player_spend_mp(1);

    int atk_min = 1 + g_player.dex / 3 + player_weapon_atk();
    int atk_max = 4 + g_player.dex / 2 + player_weapon_atk();
    int dmg = atk_min + (rand() % (atk_max - atk_min + 1));

    if (combat_enemy_category() == ENEMY_CAT_AUTOMATON) {
        dmg += atk_max + atk_max;
    }

    if (out_damage) *out_damage = (int8_t)dmg;
    snprintf(g_magic_msg, sizeof(g_magic_msg), "-%d HP", dmg);
    return true;
}

bool magic_explore_heal(void) {
    return magic_combat_heal();
}

static bool try_purge_at(int x, int y) {
    if (map_get_decor(x, y) != DECOR_STATIC_PILE) return false;
    map_set_decor(x, y, DECOR_NONE);
    return true;
}

bool magic_explore_purge(void) {
    if (g_player.spellbook < SPELL_PURGE) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO SPELL");
        return false;
    }
    if (g_player.mp < 1) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO MP");
        return false;
    }
    int x = g_player.x, y = g_player.y;
    bool hit = try_purge_at(x + 1, y) || try_purge_at(x - 1, y)
            || try_purge_at(x, y + 1) || try_purge_at(x, y - 1)
            || try_purge_at(x + facing_dx(), y + facing_dy());
    if (!hit) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO TARGET");
        return false;
    }
    player_spend_mp(1);
    snprintf(g_magic_msg, sizeof(g_magic_msg), "CLEARED!");
    save_write();
    return true;
}

static bool try_decode_at(int x, int y) {
    if (map_get_decor(x, y) != DECOR_LOCKED_DOOR) return false;
    map_set_decor(x, y, DECOR_NONE);
    return true;
}

bool magic_explore_decode(void) {
    if (g_player.spellbook < SPELL_DECODE) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO SPELL");
        return false;
    }
    if (g_player.mp < 1) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO MP");
        return false;
    }
    int x = g_player.x, y = g_player.y;
    bool hit = try_decode_at(x + 1, y) || try_decode_at(x - 1, y)
            || try_decode_at(x, y + 1) || try_decode_at(x, y - 1)
            || try_decode_at(x + facing_dx(), y + facing_dy());
    if (!hit) {
        snprintf(g_magic_msg, sizeof(g_magic_msg), "NO TARGET");
        return false;
    }
    player_spend_mp(1);
    snprintf(g_magic_msg, sizeof(g_magic_msg), "UNLOCKED!");
    save_write();
    return true;
}
