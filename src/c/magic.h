#pragma once
#include "pebble.h"
#include "player.h"

// Signal Magic — combat + explore (Heroine Dusk Burn/Unlock analogue).

// Result message buffer filled by magic_* calls (for UI).
extern char g_magic_msg[28];

// Combat spells. Returns true if the action consumed the turn.
bool magic_combat_heal(void);
bool magic_combat_purge(int8_t *out_damage);   // sets damage dealt; may break shield
bool magic_combat_decode(int8_t *out_damage);

// Explore spells from the inventory menu.
bool magic_explore_heal(void);
bool magic_explore_purge(void);   // clear adjacent static piles
bool magic_explore_decode(void);  // open adjacent locked doors

// Cycle which spell the menu/combat "spell" action targets (among known).
SpellId magic_selected_spell(void);
void    magic_cycle_spell(void);
void    magic_reset_selection(void);
