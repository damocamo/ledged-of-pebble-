#pragma once
#include "pebble.h"

void shop_init(Layer *canvas, GBitmap *icon_atlas);
bool shop_is_open(void);
void shop_open(void);
void shop_close(void);

void shop_input_up(void);
void shop_input_down(void);
void shop_input_select(void);
void shop_input_back(void);

void shop_draw(GBitmap *fb);
