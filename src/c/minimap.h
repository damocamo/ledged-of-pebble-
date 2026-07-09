#pragma once
#include "pebble.h"

// ---------------------------------------------------------------------------
// Rudimentary Legend-of-Grimrock-style auto-map.
//
// Tiles are revealed as the player walks over them (call minimap_mark every
// frame). Long-press SELECT toggles a full-screen overlay that shows the
// explored portion of the current level plus the player's position/facing.
// Exploration is tracked in RAM for the CURRENT map only and is cleared
// automatically whenever the player moves to a different map.
// ---------------------------------------------------------------------------

void minimap_init(Layer *canvas);

bool minimap_is_open(void);
void minimap_open(void);
void minimap_close(void);
void minimap_toggle(void);

// Record that the player currently occupies (x,y) on map_id. When map_id
// changes it persists the map being left and restores the explored set for
// the map being entered.
void minimap_mark(int map_id, int x, int y);

// Persist the current map's explored set (call from save_write so "Continue"
// restores what the player had already uncovered).
void minimap_persist_flush(void);

// Erase all persisted exploration (call from save_delete / New Game).
void minimap_wipe(void);

// Reveal every tile on map_id (merchant map-reveal purchase).
void minimap_reveal_all(int map_id);

// Draw the full-screen map overlay into the captured framebuffer.
void minimap_draw(GBitmap *fb);
