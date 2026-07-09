#pragma once
#include "pebble.h"

typedef enum {
    TITLE_STATE_MENU    = 0,
    TITLE_STATE_PLAYING = 1,
} TitleState;

void title_init(Layer *canvas, GBitmap *fb_ref);
bool title_is_active(void);

void title_input_up(void);
void title_input_down(void);
void title_input_select(void);

void title_draw(GBitmap *fb, GContext *ctx);