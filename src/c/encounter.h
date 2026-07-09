#pragma once
#include "pebble.h"
#include "map.h"
#include "combat.h"

void encounter_init(int map_id);
void encounter_reset(void);
bool encounter_check(void);

// Anti-farm: limited paid gold kills per enemy type per map.
bool encounter_can_award_gold(EnemyId id);
void encounter_note_gold_kill(EnemyId id);
void encounter_clear_paid_kills(void);
const uint8_t *encounter_paid_kills_blob(void);
int  encounter_paid_kills_size(void);
void encounter_load_paid_kills(const uint8_t *data, int len);
