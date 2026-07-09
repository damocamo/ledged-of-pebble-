#pragma once
#include "pebble.h"
#include "map.h"

extern GBitmap *s_atlas;

void draw_slot (GBitmap *fb, GContext *ctx, int slot, int map_x, int map_y);
void draw_decor(GBitmap *fb, GContext *ctx, int slot, int map_x, int map_y);