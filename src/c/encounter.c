#include "pebble.h"
#include "encounter.h"
#include "combat.h"
#include "player.h"
#include "save.h"

// ---- Encounter state ----------------------------------------------------
#define ENCOUNTER_INCREMENT  5
#define ENCOUNTER_MAX       30
#define ENCOUNTER_THRESHOLD 20

// Paid gold kills per enemy type per map (anti-farm). After this, fights still
// happen but drop 0 gold.
#define PAID_KILLS_PER_TYPE  2

static const MapEncounterTable s_encounters[] = {
    { .enemies = { ENEMY_RAT, ENEMY_BAT, ENEMY_RAT },                 .enemy_count = 3 },
    { .enemies = { ENEMY_BAT, ENEMY_GUARD, ENEMY_GUARD },             .enemy_count = 3 },
    { .enemies = { ENEMY_GUARD, ENEMY_SKELETON, ENEMY_BAT },          .enemy_count = 3 },
    { .enemies = { ENEMY_SKELETON, ENEMY_SPIDER, ENEMY_SKELETON },    .enemy_count = 3 },
    { .enemies = { ENEMY_SPIDER, ENEMY_SKELETON, ENEMY_WRAITH },      .enemy_count = 3 },
    { .enemies = { ENEMY_WRAITH, ENEMY_SPIDER, ENEMY_GUARD },         .enemy_count = 3 },
    { .enemies = { ENEMY_WRAITH, ENEMY_GOLEM, ENEMY_SPIDER },         .enemy_count = 3 },
    { .enemies = { ENEMY_GOLEM, ENEMY_OGRE, ENEMY_WRAITH },           .enemy_count = 3 },
    { .enemies = { ENEMY_OGRE, ENEMY_GOLEM, ENEMY_OGRE, ENEMY_WRAITH },.enemy_count = 4 },
    { .enemies = { ENEMY_OGRE, ENEMY_GOLEM, ENEMY_WRAITH, ENEMY_OGRE },.enemy_count = 4 },
};

#define ENCOUNTER_MAP_COUNT ((int)(sizeof(s_encounters)/sizeof(s_encounters[0])))

static int s_encounter_chance = 0;
static const MapEncounterTable *s_table = NULL;
static int s_map_id = 0;

// Packed: 2 bits per enemy type per map → up to 3 paid kills tracked (we use 2).
// 10 maps * 10 enemies * 2 bits = 200 bits → 25 bytes.
static uint8_t s_paid_kills[(SAVE_MAX_MAPS * ENEMY_COUNT * 2 + 7) / 8];

static int paid_index(int map_id, EnemyId id) {
    return (map_id * ENEMY_COUNT + (int)id) * 2;
}

static int get_paid_kills(int map_id, EnemyId id) {
    if (map_id < 0 || map_id >= SAVE_MAX_MAPS || id >= ENEMY_COUNT) return PAID_KILLS_PER_TYPE;
    int bit = paid_index(map_id, id);
    int byte = bit >> 3;
    int shift = bit & 7;
    return (s_paid_kills[byte] >> shift) & 0x3;
}

static void set_paid_kills(int map_id, EnemyId id, int count) {
    if (map_id < 0 || map_id >= SAVE_MAX_MAPS || id >= ENEMY_COUNT) return;
    if (count > 3) count = 3;
    int bit = paid_index(map_id, id);
    int byte = bit >> 3;
    int shift = bit & 7;
    s_paid_kills[byte] = (uint8_t)((s_paid_kills[byte] & ~(0x3 << shift)) | ((count & 0x3) << shift));
}

void encounter_init(int map_id) {
    s_map_id = map_id;
    if (map_id >= 0 && map_id < ENCOUNTER_MAP_COUNT) {
        s_table = &s_encounters[map_id];
    } else {
        s_table = NULL;
    }
    s_encounter_chance = 0;
}

void encounter_reset(void) {
    s_encounter_chance = 0;
}

void encounter_clear_paid_kills(void) {
    memset(s_paid_kills, 0, sizeof(s_paid_kills));
}

bool encounter_can_award_gold(EnemyId id) {
    // Bosses / scripted fights always pay (tracked separately via flags).
    // Random encounters: limited paid kills per type per map.
    return get_paid_kills(s_map_id, id) < PAID_KILLS_PER_TYPE;
}

void encounter_note_gold_kill(EnemyId id) {
    int n = get_paid_kills(s_map_id, id);
    if (n < PAID_KILLS_PER_TYPE) {
        set_paid_kills(s_map_id, id, n + 1);
    }
}

const uint8_t *encounter_paid_kills_blob(void) {
    return s_paid_kills;
}

int encounter_paid_kills_size(void) {
    return (int)sizeof(s_paid_kills);
}

void encounter_load_paid_kills(const uint8_t *data, int len) {
    memset(s_paid_kills, 0, sizeof(s_paid_kills));
    if (!data || len <= 0) return;
    int n = len < (int)sizeof(s_paid_kills) ? len : (int)sizeof(s_paid_kills);
    memcpy(s_paid_kills, data, (size_t)n);
}

bool encounter_check(void) {
#ifdef SCREENSHOT_NO_ENCOUNTERS
    // Capture harness only: scripted emulator walks must not be interrupted
    // by random battles.
    return false;
#endif
    if (!s_table || s_table->enemy_count == 0) return false;

    s_encounter_chance += ENCOUNTER_INCREMENT;
    if (s_encounter_chance > ENCOUNTER_MAX)
        s_encounter_chance = ENCOUNTER_MAX;

    int effective = s_encounter_chance - ENCOUNTER_THRESHOLD;
    if (effective <= 0) return false;

    int roll = rand() % 100;

    if (roll < effective) {
        s_encounter_chance = 0;
        int idx = rand() % s_table->enemy_count;
        EnemyId enemy = s_table->enemies[idx];
        combat_start(enemy, -1, true);
        return true;
    }

    return false;
}
