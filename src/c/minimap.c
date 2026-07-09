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
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_close(void) {
    s_open = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void minimap_toggle(void) {
    s_open = !s_open;
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

void minimap_draw(GBitmap *fb) {
    if (!s_open) return;

    GRect b = gbitmap_get_bounds(fb);
    int W = b.size.w, H = b.size.h;

    // Opaque backdrop over the 3D view.
    fb_fill(fb, 0, 0, W, H, GColorBlack);

    int cx = W / 2;
    static char title[16];
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

            GColor c;
            uint8_t d = map_get_decor(x, y);
            if (t == TILE_DOOR || d == DECOR_DOOR || d == DECOR_GATE) {
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

    // Footer hint — anchored so its full 24px height stays on-screen.
    bitfont_render(fb, "SEL:CLOSE", cx, H - MM_TEXT_H - 2, JUSTIFY_CENTERV);
}
