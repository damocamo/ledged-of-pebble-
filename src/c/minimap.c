#include "pebble.h"
#include "minimap.h"
#include "map.h"
#include "player.h"
#include "bitfont.h"
#include "save.h"     // SAVE_MAX_MAPS

#define MM_MIN(a,b) ((a) < (b) ? (a) : (b))

// Persist keys for exploration: one per map, kept clear of the main-save key
// (1) and the per-map delta keys (2..2+SAVE_MAX_MAPS-1).
#define MM_PERSIST_BASE 20

// ---- State --------------------------------------------------------------
static bool   s_open        = false;
static bool   s_legend      = false;   // UP toggles the marker legend page
static Layer *s_canvas_ref  = NULL;
static int    s_cur_map     = -1;

// Explored tiles for the current map, packed 1 bit per cell to keep RAM tiny.
#define VIS_BYTES ((MAP_W * MAP_H + 7) / 8)
static uint8_t s_visited[VIS_BYTES];

static void mm_flush(void) {
    if (s_cur_map < 0) return;
    persist_write_data(MM_PERSIST_BASE + s_cur_map, s_visited, sizeof(s_visited));
}

static void mm_load(int map_id) {
    memset(s_visited, 0, sizeof(s_visited));
    int key = MM_PERSIST_BASE + map_id;
    if (persist_exists(key)) {
        persist_read_data(key, s_visited, sizeof(s_visited));
    }
}

static inline bool vis_get(int x, int y) {
    int i = y * MAP_W + x;
    return (s_visited[i >> 3] >> (i & 7)) & 1;
}
static inline void vis_set(int x, int y) {
    int i = y * MAP_W + x;
    s_visited[i >> 3] |= (uint8_t)(1 << (i & 7));
}

// ---- Public API ---------------------------------------------------------
void minimap_init(Layer *canvas) {
    s_canvas_ref = canvas;
    s_open       = false;
    s_cur_map    = -1;
    memset(s_visited, 0, sizeof(s_visited));
}

bool minimap_is_open(void) { return s_open; }

void minimap_open(void) {
    s_open = true;
    s_legend = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_close(void) {
    s_open = false;
    s_legend = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_toggle(void) {
    s_open = !s_open;
    s_legend = false;
#ifdef SCREENSHOT_REVEAL
    // Capture harness only: opening the map acts like buying MAP REVEAL.
    if (s_open) minimap_reveal_all(g_player.map_id);
#endif
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_input_up(void) {
    if (!s_open) return;
    s_legend = !s_legend;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_mark(int map_id, int x, int y) {
    if (map_id != s_cur_map) {
        if (s_cur_map >= 0) mm_flush();  // persist the map we're leaving
        s_cur_map = map_id;
        mm_load(map_id);                 // restore what was explored here
    }
    if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) {
        vis_set(x, y);
    }
}

void minimap_persist_flush(void) {
    mm_flush();
}

void minimap_wipe(void) {
    for (int m = 0; m < SAVE_MAX_MAPS; m++) {
        persist_delete(MM_PERSIST_BASE + m);
    }
    memset(s_visited, 0, sizeof(s_visited));
    s_cur_map = -1;
}

void minimap_reveal_all(int map_id) {
    if (map_id < 0 || map_id >= SAVE_MAX_MAPS) return;
    if (map_id != s_cur_map) {
        if (s_cur_map >= 0) mm_flush();
        s_cur_map = map_id;
        mm_load(map_id);
    }
    memset(s_visited, 0xFF, sizeof(s_visited));
    mm_flush();
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

// ---- Drawing helpers ----------------------------------------------------
static void fb_fill(GBitmap *fb, int x0, int y0, int w, int h, GColor c) {
    GRect b = gbitmap_get_bounds(fb);
    for (int yy = y0; yy < y0 + h; yy++) {
        if (yy < 0 || yy >= b.size.h) continue;
        GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, yy);
        int xs = x0;
        int xe = x0 + w - 1;
        if (xs < info.min_x) xs = info.min_x;
        if (xe > info.max_x) xe = info.max_x;
        for (int xx = xs; xx <= xe; xx++) info.data[xx] = c.argb;
    }
}

static bool is_wall_tile(uint8_t t) {
    return t == TILE_WALL || t == TILE_PILLAR || t == TILE_SECRET;
}

// A cell is shown once it has been visited or borders a visited cell, so the
// walls that enclose explored corridors read correctly (Grimrock-style).
static bool cell_revealed(int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (vis_get(nx, ny)) return true;
        }
    }
    return false;
}

#define MM_TEXT_H  24   // bitfont glyph height (FONT_HEIGHT * FONT_SCALE)

// Legend page: what each marker color means. Rows use the exact argb values
// the map draws with so the swatches always match.
typedef struct { uint8_t argb; const char *label; } LegendRow;

static void draw_legend(GBitmap *fb) {
    static const LegendRow k_rows[] = {
        { 0b11110000, "YOU"        },   // red (player marker)
        { 0b11001100, "MERCHANT"   },   // green
        { 0b11111000, "CHEST"      },   // orange
        { 0b11010111, "REST BED"   },   // blue
        { 0b11111100, "DOOR/GATE"  },   // yellow
        { 0b11001111, "SWITCH/KEY" },   // cyan
        { 0b11110011, "TELEPORT"   },   // magenta
        { 0b11011101, "STATIC"     },   // bright green
        { 0b11010000, "PIT"        },   // dark red
        { 0b11100100, "BLOCK"      },   // tan
    };
    GRect b = gbitmap_get_bounds(fb);
    int W = b.size.w, H = b.size.h;
    fb_fill(fb, 0, 0, W, H, GColorBlack);

    // Ten rows at 17px need a tall screen; on shorter displays (gabbro,
    // 168px) drop to scale-1 text so the whole key still fits.
    const bool big   = (H >= 210);
    const int  scale = big ? 2 : 1;
    const int  row_h = big ? 17 : 12;
    const int  sw    = big ? 14 : 9;            // swatch size

    bitfont_render(fb, "MAP KEY", W / 2, 2, JUSTIFY_CENTERV);

    const int rows = (int)ARRAY_LENGTH(k_rows);
    int y    = MM_TEXT_H + (big ? 8 : 4);
    int sw_x = big ? 16 : 10;                   // swatch column
    int tx   = sw_x + sw + 8;                   // label column

    for (int i = 0; i < rows; i++) {
        GColor c; c.argb = k_rows[i].argb;
        fb_fill(fb, sw_x, y + 1, sw, sw, c);
        bitfont_render_scaled(fb, k_rows[i].label, tx, y, JUSTIFY_LEFT, scale);
        y += row_h;
    }

    bitfont_render_scaled(fb, "UP:MAP  SEL:CLOSE", W / 2,
                          H - 10 * scale, JUSTIFY_CENTERV, scale);
}

void minimap_draw(GBitmap *fb) {
    if (!s_open) return;

    if (s_legend) {
        draw_legend(fb);
        return;
    }

    GRect b = gbitmap_get_bounds(fb);
    int W = b.size.w, H = b.size.h;

    // Opaque backdrop over the 3D view.
    fb_fill(fb, 0, 0, W, H, GColorBlack);

    int cx = W / 2;
    static char title[20];
    snprintf(title, sizeof(title), "MAP  L%d", g_player.map_id + 1);
    bitfont_render(fb, title, cx, 2, JUSTIFY_CENTERV);

    // Bounding box of everything explored so we can zoom the map to fill the
    // screen instead of drawing the whole sparse 28x28 grid tiny.
    int minx = MAP_W, miny = MAP_H, maxx = -1, maxy = -1;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (!vis_get(x, y)) continue;
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    if (maxx < 0) { minx = maxx = g_player.x; miny = maxy = g_player.y; }
    // pad by one so the walls bordering explored floor are visible
    if (minx > 0)         minx--;
    if (miny > 0)         miny--;
    if (maxx < MAP_W - 1) maxx++;
    if (maxy < MAP_H - 1) maxy++;
    int bw = maxx - minx + 1;
    int bh = maxy - miny + 1;

    // Drawable region sits between the title and the footer.
    int top     = MM_TEXT_H + 6;
    int bottom  = H - MM_TEXT_H - 6;
    int avail_w = W - 6;
    int avail_h = bottom - top;

    // Scale cells up to fill the limiting screen dimension (Grimrock-style
    // zoom-to-explored). No upper cap so the map uses the whole screen.
    int cell = MM_MIN(avail_w / bw, avail_h / bh);
    if (cell < 2) cell = 2;

    int grid_w = cell * bw;
    int grid_h = cell * bh;
    int ox = (W - grid_w) / 2;
    int oy = top + (avail_h - grid_h) / 2;

    GColor c_wall  = GColorDarkGray;
    GColor c_floor = GColorLightGray;
    GColor c_door  = GColorYellow;
    GColor c_you   = GColorRed;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            if (!cell_revealed(x, y)) continue;

            uint8_t t = map_get_tile(x, y);
            if (t == TILE_BLANK) continue;   // void — leave black

            // Landmark decor you've walked past gets its own marker color
            // (never enemies — encounters aren't map decor). Reveal follows
            // the same rule as tiles: visited or bordering a visited cell.
            // 0 = not a landmark; fall back to tile color.
            static const uint8_t k_decor_argb[DECOR_TYPE_COUNT] = {
                [DECOR_DOOR]         = 0b11111100,  // yellow
                [DECOR_GATE]         = 0b11111100,
                [DECOR_LOCKED_DOOR]  = 0b11111100,
                [DECOR_MAREN]        = 0b11001100,  // green — merchant
                [DECOR_CHEST]        = 0b11111000,  // orange
                [DECOR_HAYBED]       = 0b11010111,  // blue — rest
                [DECOR_TELEPORT]     = 0b11110011,  // magenta
                [DECOR_PIT]          = 0b11010000,  // dark red
                [DECOR_STATIC_PILE]  = 0b11011101,  // bright green
                [DECOR_BLOCK]        = 0b11100100,  // tan
                [DECOR_PLATE_UP]     = 0b11001111,  // cyan — switches/locks
                [DECOR_PLATE_DOWN]   = 0b11001111,
                [DECOR_SWITCH_DOWN]  = 0b11001111,
                [DECOR_SWITCH_UP]    = 0b11001111,
                [DECOR_F_SWITCH]     = 0b11001111,
                [DECOR_KEYHOLE]      = 0b11001111,
                [DECOR_ALCOVE_EMPTY] = 0b11001111,
                [DECOR_ALCOVE_FULL]  = 0b11001111,
            };
            GColor c;
            uint8_t d = map_get_decor(x, y);
            uint8_t argb = (d < DECOR_TYPE_COUNT) ? k_decor_argb[d] : 0;
            if (argb) {
                c.argb = argb;
            } else if (t == TILE_DOOR) {
                c = c_door;
            } else if (is_wall_tile(t)) {
                c = c_wall;
            } else {
                c = c_floor;
            }

            int px = ox + (x - minx) * cell;
            int py = oy + (y - miny) * cell;
            fb_fill(fb, px, py, cell - 1, cell - 1, c);
        }
    }

    // Player marker + facing notch.
    int px = ox + (g_player.x - minx) * cell;
    int py = oy + (g_player.y - miny) * cell;
    fb_fill(fb, px, py, cell - 1, cell - 1, c_you);
    int mid = (cell - 1) / 2;
    switch (g_player.facing) {
        case FACING_NORTH: fb_fill(fb, px + mid, py,         1, mid, GColorWhite); break;
        case FACING_SOUTH: fb_fill(fb, px + mid, py + mid,   1, mid, GColorWhite); break;
        case FACING_WEST:  fb_fill(fb, px,       py + mid, mid,   1, GColorWhite); break;
        case FACING_EAST:  fb_fill(fb, px + mid, py + mid, mid,   1, GColorWhite); break;
        default: break;
    }

    // Footer hint — anchored so its full height stays on-screen.
    bitfont_render_scaled(fb, "UP:KEY  SEL:CLOSE", cx, H - 20,
                          JUSTIFY_CENTERV, 2);
}
