// bitfont.h
#pragma once
#include "pebble.h"

typedef enum {
    JUSTIFY_LEFT   = 0,
    JUSTIFY_CENTER = 1,
    JUSTIFY_RIGHT  = 2,
    JUSTIFY_CENTERV  = 3,
} BitfontJustify;

// Default scale used by bitfont_render / bitfont_calc_* (no scale arg).
#define BITFONT_DEFAULT_SCALE 3

// Renders text with \n support at the default scale.
// x,y is the CENTER of the whole text block when JUSTIFY_CENTER
// For JUSTIFY_LEFT/RIGHT, x is the left/right anchor, y is top of block
void bitfont_render(GBitmap *fb, const char *text,
                    int x, int y, BitfontJustify justify);

// Same as bitfont_render, but with an explicit pixel scale (1..4).
void bitfont_render_scaled(GBitmap *fb, const char *text,
                           int x, int y, BitfontJustify justify, int scale);

// Returns total height of a multi-line string (default scale)
int bitfont_calc_height(const char *text);
int bitfont_calc_width(const char *text);

// Scaled variants
int bitfont_calc_height_scaled(const char *text, int scale);
int bitfont_calc_width_scaled(const char *text, int scale);
