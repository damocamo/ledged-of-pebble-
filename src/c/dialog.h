#pragma once
#include "pebble.h"

#define DIALOG_MAX_PAGES  16

typedef struct {
    const char *text;
} DialogPage;

typedef struct {
    const char       *title;
    const DialogPage *pages;
    int               page_count;
} DialogDef;

// Open a dialog — call from an event command
void dialog_init(Layer *canvas);   // add this line
void dialog_open(const DialogDef *def);

// Returns true if dialog is currently active
bool dialog_is_open(void);


// Input handlers — call from your click handlers in main.c
void dialog_input_next(void);
void dialog_input_prev(void);
void dialog_input_exit(void);

// Draw dialog overlay — call in draw_view before frame buffer release
void dialog_draw(GBitmap *fb);