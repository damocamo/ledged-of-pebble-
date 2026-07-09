#include "bitfont.h"
#include <string.h>
#include <ctype.h>

// ============================================================
// bitfont.c
// Adapted from Heroine Dusk Bitfont.js
// https://github.com/clintbellanger/heroine-dusk/blob/master/release/js/bitfont.js
//
// ============================================================

#define FONT_HEIGHT  8
#define FONT_KERNING (-1)
#define FONT_SPACE    3
#define FONT_SCALE    BITFONT_DEFAULT_SCALE
#define MAX_LINES 8
#define LINE_SPACING  3   // extra px between lines

typedef struct {
    uint16_t x;   // pixel x in atlas font row
    uint8_t  w;   // glyph width
} Glyph;


#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// Glyph table — uppercase only, index by (char - '!')
// Matches the boxy_bold layout from bitfont.js
static const Glyph s_glyphs[] = {
    /* ! */ {   0,  4 },
    /* " */ {   5,  7 },
    /* # */ {  13,  9 },
    /* $ */ {  23,  7 },
    /* % */ {  31, 10 },
    /* & */ {  42,  9 },
    /* ' */ {  52,  4 },
    /* ( */ {  57,  5 },
    /* ) */ {  63,  5 },
    /* * */ {  69,  6 },
    /* + */ {  76,  8 },
    /* , */ {  85,  5 },
    /* - */ {  91,  6 },
    /* . */ {  98,  4 },
    /* / */ { 103,  6 },
    /* 0 */ { 110,  7 },
    /* 1 */ { 118,  4 },
    /* 2 */ { 123,  7 },
    /* 3 */ { 131,  7 },
    /* 4 */ { 139,  7 },
    /* 5 */ { 147,  7 },
    /* 6 */ { 155,  7 },
    /* 7 */ { 163,  7 },
    /* 8 */ { 171,  7 },
    /* 9 */ { 179,  7 },
    /* : */ { 187,  4 },
    /* ; */ { 192,  4 },
    /* < */ { 197,  6 },
    /* = */ { 204,  6 },
    /* > */ { 211,  6 },
    /* ? */ { 218,  8 },
    /* @ */ { 227,  8 },
    /* A */ { 236,  7 },
    /* B */ { 244,  7 },
    /* C */ { 252,  7 },
    /* D */ { 260,  7 },
    /* E */ { 268,  7 },
    /* F */ { 276,  7 },
    /* G */ { 284,  7 },
    /* H */ { 292,  7 },
    /* I */ { 300,  4 },
    /* J */ { 305,  7 },
    /* K */ { 313,  7 },
    /* L */ { 321,  7 },
    /* M */ { 329,  9 },
    /* N */ { 339,  8 },
    /* O */ { 348,  7 },
    /* P */ { 356,  7 },
    /* Q */ { 364,  8 },
    /* R */ { 373,  7 },
    /* S */ { 381,  7 },
    /* T */ { 389,  8 },
    /* U */ { 398,  7 },
    /* V */ { 406,  7 },
    /* W */ { 414,  9 },
    /* X */ { 424,  7 },
    /* Y */ { 432,  8 },
    /* Z */ { 441,  7 },
    /* [ */ { 449,  5 },
    /* \ */ { 455,  6 },
    /* ] */ { 462,  5 },
    /* ^ */ { 468,  8 },
};

#define GLYPH_FIRST '!'
#define GLYPH_LAST  '^'
#define GLYPH_COUNT ((GLYPH_LAST - GLYPH_FIRST) + 1)

// ---- Atlas reference ----------------------------------------------------
#define FONT_ATLAS_Y 0

extern GBitmap *s_bitfont;

static int calc_width_unscaled(const char *text) {
    int total = 0;
    for (int i = 0; text[i]; i++) {
        char c = toupper((unsigned char)text[i]);
        if (c == ' ') {
            total += FONT_SPACE;
        } else if (c >= GLYPH_FIRST && c <= GLYPH_LAST) {
            total += s_glyphs[c - GLYPH_FIRST].w + FONT_KERNING;
        }
    }
    if (total > 0) total -= FONT_KERNING;
    return total;
}

static int split_lines(const char *text, const char **lines, int *widths,
                       int max, int scale) {
    static char buf[64];  /* dialog/message lines fit well under 64 */
    strncpy(buf, text, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    int count = 0;
    char *p = buf;
    char *start = buf;

    while (*p && count < max) {
        if (*p == '\n') {
            *p = '\0';
            lines[count]  = start;
            widths[count] = calc_width_unscaled(start) * scale;
            count++;
            start = p + 1;
        }
        p++;
    }
    if (count < max) {
        lines[count]  = start;
        widths[count] = calc_width_unscaled(start) * scale;
        count++;
    }
    return count;
}

int bitfont_calc_height_scaled(const char *text, int scale) {
    if (!text || scale < 1) return 0;
    int lines = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n') lines++;
    return lines * FONT_HEIGHT * scale + (lines - 1) * LINE_SPACING;
}

int bitfont_calc_height(const char *text) {
    return bitfont_calc_height_scaled(text, FONT_SCALE);
}

int bitfont_calc_width_scaled(const char *text, int scale) {
    if (!text || scale < 1) return 0;
    return calc_width_unscaled(text) * scale;
}

int bitfont_calc_width(const char *text) {
    return bitfont_calc_width_scaled(text, FONT_SCALE);
}

// ---- Internal blit ------------------------------------------------------

static void blit_glyph(GBitmap *fb, int src_x, int src_w,
                       int dest_x, int dest_y, int scale) {
    uint8_t *atlas_data   = gbitmap_get_data(s_bitfont);
    int      atlas_stride = gbitmap_get_bytes_per_row(s_bitfont);
    GColor  *palette      = gbitmap_get_palette(s_bitfont);
    GRect    fb_bounds    = gbitmap_get_bounds(fb);

    // Cap scale so the stack buffer stays bounded (max glyph w=10, scale<=4)
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        int fb_y = dest_y + row * scale;

        uint8_t row_buf[40];      // 10 * 4
        bool    row_opaque[40];
        int draw_w = src_w * scale;

        for (int sx = 0; sx < src_w; sx++) {
            int map_x = src_x + sx;
            int map_y = FONT_ATLAS_Y + row;
            uint8_t byte  = atlas_data[map_y * atlas_stride + (map_x / 2)];
            uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
            GColor  col   = palette[index];

            uint8_t argb = col.argb;
            bool    opaque = (col.a != 0);

            for (int s = 0; s < scale; s++) {
                row_buf[sx * scale + s] = argb;
                row_opaque[sx * scale + s] = opaque;
            }
        }

        for (int dy = 0; dy < scale; dy++) {
            int y = fb_y + dy;
            if (y < 0 || y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
            if (info.min_x > info.max_x) continue;

            int copy_start = MAX(0, info.min_x - dest_x);
            int copy_end   = MIN(draw_w, info.max_x - dest_x + 1);
            if (copy_start >= copy_end) continue;

            for (int x = copy_start; x < copy_end; x++) {
                if (row_opaque[x])
                    info.data[dest_x + x] = row_buf[x];
            }
        }
    }
}

// ---- Public API ---------------------------------------------------------

void bitfont_render_scaled(GBitmap *fb, const char *text,
                           int x, int y, BitfontJustify justify, int scale) {
    if (!text || !s_bitfont) return;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    const char *lines[MAX_LINES];
    int widths[MAX_LINES];
    int line_count = split_lines(text, lines, widths, MAX_LINES, scale);

    int line_h  = FONT_HEIGHT * scale;
    int total_h = bitfont_calc_height_scaled(text, scale);

    int y_start;
    if (justify == JUSTIFY_CENTER) {
        y_start = y - total_h / 2;
    } else {
        y_start = y;
    }

    for (int i = 0; i < line_count; i++) {
        int line_y = y_start + i * (line_h + LINE_SPACING);
        int line_x;

        if (justify == JUSTIFY_CENTER || justify == JUSTIFY_CENTERV) {
            line_x = x - widths[i] / 2;
        } else if (justify == JUSTIFY_RIGHT) {
            line_x = x - widths[i];
        } else {
            line_x = x;
        }

        int cursor_x = line_x;
        const char *p = lines[i];
        while (*p) {
            char c = toupper((unsigned char)*p);
            if (c == ' ') {
                cursor_x += FONT_SPACE * scale;
            } else if (c >= GLYPH_FIRST && c <= GLYPH_LAST) {
                const Glyph *g = &s_glyphs[c - GLYPH_FIRST];
                blit_glyph(fb, g->x, g->w, cursor_x, line_y, scale);
                cursor_x += (g->w + FONT_KERNING) * scale;
            }
            p++;
        }
    }
}

void bitfont_render(GBitmap *fb, const char *text,
                    int x, int y, BitfontJustify justify) {
    bitfont_render_scaled(fb, text, x, y, justify, FONT_SCALE);
}
