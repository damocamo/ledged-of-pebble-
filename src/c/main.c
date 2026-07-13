#include "pebble.h"
#include "bitfont.h"
#include "event.h"
#include "map.h"
#include "dialog.h"
#include "menu.h"
#include "player.h"
#include "combat.h"
#include "encounter.h"
#include "title.h"
#include "save.h"
#include "touch.h"
#include "puzzle.h"
#include "minimap.h"
#include "shop.h"
#include "magic.h"

static Window  *s_window;
static Layer   *s_canvas;
static GBitmap *s_atlas;
GBitmap *s_bitfont;  // add this
GBitmap *s_icon_atlas;
GBitmap *s_monsters;  // no static — combat.c needs it

// Monster sprites are loaded one 58x63 slice at a time: the full sheet
// (~20KB as a GBitmap) no longer fits on the heap next to the tile atlas,
// which made gbitmap_create_with_resource fail silently and enemies render
// invisible. combat_start()/shop_open() load the slot they need.
static int s_monster_slot = -1;
static const uint32_t MONSTER_RESOURCES[] = {
    RESOURCE_ID_MONSTER_0, RESOURCE_ID_MONSTER_1, RESOURCE_ID_MONSTER_2,
    RESOURCE_ID_MONSTER_3, RESOURCE_ID_MONSTER_4, RESOURCE_ID_MONSTER_5,
    RESOURCE_ID_MONSTER_6, RESOURCE_ID_MONSTER_7, RESOURCE_ID_MONSTER_8,
    RESOURCE_ID_MONSTER_9, RESOURCE_ID_MONSTER_10,
};

void monster_bitmap_load(int slot) {
    if (slot < 0 || slot >= (int)ARRAY_LENGTH(MONSTER_RESOURCES)) return;
    if (s_monsters && s_monster_slot == slot) return;
    if (s_monsters) gbitmap_destroy(s_monsters);
    s_monsters = gbitmap_create_with_resource(MONSTER_RESOURCES[slot]);
    s_monster_slot = s_monsters ? slot : -1;
}

void monster_bitmap_unload(void) {
    if (s_monsters) {
        gbitmap_destroy(s_monsters);
        s_monsters = NULL;
    }
    s_monster_slot = -1;
}

typedef struct {
    int dx, dy;   // map offset from player
} ViewSlot;

#define BLIT_ORIGIN_X (fb_bounds.size.w / 2 - 130 + g_shake_offset.x)
#define BLIT_ORIGIN_Y (fb_bounds.size.h / 2 + g_shake_offset.y)

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// #define TIME_START() uint16_t _t0 = (uint16_t)(time_ms(NULL, NULL))
// #define TIME_END(label) APP_LOG(APP_LOG_LEVEL_DEBUG, label ": %u ms", 
//     (uint16_t)(time_ms(NULL, NULL)) - _t0)

static void fb_put(GBitmap *fb, int x, int y, GColor c) {
    GRect b = gbitmap_get_bounds(fb);
    if (y < 0 || y >= b.size.h) return;
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    if (x < info.min_x || x > info.max_x) return;
    info.data[x] = c.argb;
}

static void fb_fill_rect(GBitmap *fb, int x0, int y0, int w, int h, GColor c) {
    for (int yy = y0; yy < y0 + h; yy++)
        for (int xx = x0; xx < x0 + w; xx++)
            fb_put(fb, xx, yy, c);
}

// Small top-right compass: ring + N tick + needle pointing the way the
// player is facing. Cleaner than spelling out NORTH/EAST/SOUTH/WEST.
static void draw_compass(GBitmap *fb, Facing facing) {
    GRect b = gbitmap_get_bounds(fb);
    const int R  = 7;                 // outer radius
    const int cx = b.size.w - R - 4;  // top-right with a little margin
    const int cy = R + 3;

    // Soft backdrop so the needle reads on any wall colour
    fb_fill_rect(fb, cx - R - 1, cy - R - 1, 2 * R + 3, 2 * R + 3, GColorBlack);

    // Ring (diamond-ish circle via manhattan/euclidean mix)
    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= (R - 1) * (R - 1) && d2 <= R * R)
                fb_put(fb, cx + dx, cy + dy, GColorLightGray);
        }
    }

    // Fixed "N" tick at the top of the ring
    fb_fill_rect(fb, cx - 1, cy - R + 1, 3, 2, GColorWhite);

    // Needle: short shaft + tip in the facing direction.
    // Tip is chrome-yellow so it pops against the teal/amber world palette.
    int tip_x = cx, tip_y = cy;
    switch (facing) {
        case FACING_NORTH:
            tip_x = cx; tip_y = cy - 5;
            fb_fill_rect(fb, cx, cy - 4, 1, 4, GColorWhite);
            break;
        case FACING_EAST:
            tip_x = cx + 5; tip_y = cy;
            fb_fill_rect(fb, cx, cy, 4, 1, GColorWhite);
            break;
        case FACING_SOUTH:
            tip_x = cx; tip_y = cy + 5;
            fb_fill_rect(fb, cx, cy, 1, 4, GColorWhite);
            break;
        case FACING_WEST:
            tip_x = cx - 5; tip_y = cy;
            fb_fill_rect(fb, cx - 4, cy, 4, 1, GColorWhite);
            break;
        default: break;
    }
    // Tip (3x3 so it reads at watch scale)
    fb_fill_rect(fb, tip_x - 1, tip_y - 1, 3, 3, GColorChromeYellow);
    // Hub
    fb_put(fb, cx, cy, GColorWhite);
}

#define SCALE 2
#define TILE_OPEN     0
#define TILE_FLOOR    1
#define TILE_FLOOR_R  2
#define TILE_WALL     3

static GColor sample_atlas(uint8_t *atlas_data, int atlas_stride,
                            GColor *palette, int x, int y) {
    uint8_t byte  = atlas_data[y * atlas_stride + (x / 2)];
    uint8_t index = (x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
    GColor  col   = palette[index];
    //col.a = 3;  // force opaque when writing to framebuffer
    return col;
}

// static uint8_t get_atlas_pixel(uint8_t *atlas_data, int atlas_stride, 
//                                 int x, int y) {
//     uint8_t byte = atlas_data[y * atlas_stride + (x / 2)];
//     if (x % 2 == 0) {
//         return (byte >> 4) & 0x0F;  // high nibble = even pixel
//     } else {
//         return byte & 0x0F;          // low nibble  = odd pixel
//     }
// }

// Single blit core: the four public variants only differ in whether the
// source is mirrored horizontally (flip_h) and/or the sprite is drawn
// ceiling-mirrored (flip_v, which also flips the destination Y mapping).
// Folding them saves ~1KB of code on a 64KB budget.
static void blit_core(GBitmap *fb,
                      int src_x, int src_y, int src_w, int src_h,
                      int dest_x, int dest_y, int scale,
                      bool flip_h, bool flip_v) {
  int draw_w = src_w * scale;
  int draw_h = src_h * scale;

  uint8_t *atlas_data   = gbitmap_get_data(s_atlas);
  int      atlas_stride = gbitmap_get_bytes_per_row(s_atlas);
  GColor  *palette      = gbitmap_get_palette(s_atlas);

  GRect fb_bounds = gbitmap_get_bounds(fb);
  const int fb_x_start = fb_bounds.size.w / 2 - 130 + dest_x + g_shake_offset.x;

  uint8_t row_buf[draw_w];
  bool    row_opaque[draw_w];

  for (int y = 0; y < draw_h; y += scale) {
    int row = y / scale;
    int map_y = src_y + (flip_v ? (src_h - 1 - row) : row);

    for (int sx = 0; sx < src_w; sx++) {
      int map_x = src_x + (flip_h ? (src_w - 1 - sx) : sx);
      GColor col = sample_atlas(atlas_data, atlas_stride, palette, map_x, map_y);
      uint8_t argb = col.argb;  // no sentinel, take value as-is
      bool    opaque = (col.a != 0);
      uint16_t doubled = (uint16_t)argb | ((uint16_t)argb << 8);
      *(uint16_t*)(&row_buf[sx * 2])     = doubled;
      row_opaque[sx * 2]     = opaque;
      row_opaque[sx * 2 + 1] = opaque;
    }

    for (int dy = 0; dy < scale; dy++) {
      int fb_y = flip_v
        ? fb_bounds.size.h/2 - dest_y - draw_h + y + dy + 2 + g_shake_offset.y
        : fb_bounds.size.h/2 + dest_y + y + dy + g_shake_offset.y;
      if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
      GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);
      if (info.min_x > info.max_x) continue;

      int copy_start = MAX(0, info.min_x - fb_x_start);
      int copy_end   = MIN(draw_w, info.max_x - fb_x_start + 1);
      if (copy_start >= copy_end) continue;

      for (int x = copy_start; x < copy_end; x++) {
        if (row_opaque[x])
            info.data[fb_x_start + x] = row_buf[x];
      }
    }
  }
}

void blit_scaled(GBitmap *fb, GContext *ctx,
                        int src_x, int src_y, int src_w, int src_h,
                        int dest_x, int dest_y, int scale) {
  (void)ctx;
  blit_core(fb, src_x, src_y, src_w, src_h, dest_x, dest_y, scale, false, false);
}

void blit_scaled_flipv(GBitmap *fb, GContext *ctx,
                              int src_x, int src_y, int src_w, int src_h,
                              int dest_x, int dest_y, int scale) {
  (void)ctx;
  blit_core(fb, src_x, src_y, src_w, src_h, dest_x, dest_y, scale, false, true);
}

void blit_scaled_fliphv(GBitmap *fb, GContext *ctx,
                              int src_x, int src_y, int src_w, int src_h,
                              int dest_x, int dest_y, int scale) {
  (void)ctx;
  blit_core(fb, src_x, src_y, src_w, src_h, dest_x, dest_y, scale, true, true);
}

void blit_scaled_flipped(GBitmap *fb, GContext *ctx,
                                int src_x, int src_y, int src_w, int src_h,
                                int dest_x, int dest_y, int scale) {
  (void)ctx;
  blit_core(fb, src_x, src_y, src_w, src_h, dest_x, dest_y, scale, true, false);
}

  
void draw_slot(GBitmap *fb, GContext *ctx, int slot, int map_x, int map_y) {
    // Bounds check
    if (map_x < 0 || map_x >= MAP_W) return;
    if (map_y < 0 || map_y >= MAP_H) return;

    uint8_t tile_type = map_get_tile(map_x, map_y);
    if (tile_type >= TILE_TYPE_COUNT) return;

    const TileView *v = &TILE_VIEWS[tile_type][slot];
    if (v->src_w == 0) return;  // TV_NONE — nothing to draw
  
    // Floor (below center)
    if (v->flip) {
        blit_scaled_flipped(fb, ctx,
            v->src_x, v->src_y, v->src_w, v->src_h,
            v->dest_x, v->dest_y, SCALE);
        if (v->draw_ceiling)
          blit_scaled_fliphv(fb, ctx,
              v->src_x, v->src_y, v->src_w, v->src_h,
              v->dest_x, v->dest_y, SCALE);
    } else {
        blit_scaled(fb, ctx,
            v->src_x, v->src_y, v->src_w, v->src_h,
            v->dest_x, v->dest_y, SCALE);
        if (v->draw_ceiling)
          blit_scaled_flipv(fb, ctx,
              v->src_x, v->src_y, v->src_w, v->src_h,
              v->dest_x, v->dest_y, SCALE);
    }
}

void draw_decor(GBitmap *fb, GContext *ctx, int slot, int map_x, int map_y) {
    if (map_x < 0 || map_x >= MAP_W) return;
    if (map_y < 0 || map_y >= MAP_H) return;

    uint8_t decor_type = map_get_decor(map_x, map_y);  // bounds checked inside
    if (decor_type == DECOR_NONE) return;
    if (decor_type >= DECOR_TYPE_COUNT) return;

    const DecorView *v = &DECOR_VIEWS[decor_type][slot];
    if (v->src_w == 0) return;

    if (v->flip) {
        if (!v->draw_ceiling)
            blit_scaled_flipped(fb, ctx,
                v->src_x, v->src_y, v->src_w, v->src_h,
                v->dest_x, v->dest_y, SCALE);
        else
            blit_scaled_fliphv(fb, ctx,
                v->src_x, v->src_y, v->src_w, v->src_h,
                v->dest_x, v->dest_y, SCALE);
    } else {
        if (!v->draw_ceiling)
            blit_scaled(fb, ctx,
                v->src_x, v->src_y, v->src_w, v->src_h,
                v->dest_x, v->dest_y, SCALE);
        else
            blit_scaled_flipv(fb, ctx,
                v->src_x, v->src_y, v->src_w, v->src_h,
                v->dest_x, v->dest_y, SCALE);
    }
}

static void draw_view(GContext *ctx, Layer *layer) {
    //TIME_START();
    GBitmap *fb = graphics_capture_frame_buffer(ctx);

    if (title_is_active()) {
        title_draw(fb, ctx);
    }else{
        // record the player's current tile for the auto-map
        minimap_mark(g_player.map_id, g_player.x, g_player.y);

        for (int slot = 0; slot < VIEW_SLOT_COUNT; slot++) {
            Offset off = VIEW_OFFSETS[slot][g_player.facing];
            int map_x  = g_player.x + off.dx;
            int map_y  = g_player.y + off.dy;

            // Base tile
            draw_slot(fb, ctx, slot, map_x, map_y);

            // Decoration overlay on top
            draw_decor(fb, ctx, slot, map_x, map_y);
        }

        if (minimap_is_open()) {
            minimap_draw(fb);
        }
        else if (combat_is_active()) {
            combat_draw(fb);
        } 
        else if (dialog_is_open()) {
            dialog_draw(fb);
        } 
        else if (menu_is_open()) {
            menu_draw(fb);
        }
        else if (shop_is_open()) {
            shop_draw(fb);
        }
        else {
            // Low-HP feedback: amber corner ticks when badly hurt
            if (player_is_hurt()) {
                GRect b = gbitmap_get_bounds(fb);
                fb_fill_rect(fb, 0, 0, 6, 2, GColorChromeYellow);
                fb_fill_rect(fb, b.size.w - 6, 0, 6, 2, GColorChromeYellow);
                fb_fill_rect(fb, 0, b.size.h - 2, 6, 2, GColorChromeYellow);
                fb_fill_rect(fb, b.size.w - 6, b.size.h - 2, 6, 2, GColorChromeYellow);
            }
            // normal HUD — compass instead of NORTH/EAST/SOUTH/WEST text
            draw_compass(fb, g_player.facing);
            event_draw_message(fb);
            event_draw_icon(fb);
        }
    }

    graphics_release_frame_buffer(ctx, fb);
    //TIME_END("blit total");
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    draw_view(ctx, layer);
}

// void event_check_tile(int map_x, int map_y) {
//     if (map_x < 0 || map_x >= MAP_W) return;
//     if (map_y < 0 || map_y >= MAP_H) return;

//     int8_t event_id = s_event_map[map_y][map_x];
//     if (event_id < 0) return;

//     event_run(event_id);
// }



static void up_click(ClickRecognizerRef recognizer, void *ctx) {
    // Move forward in facing direction

    if (title_is_active()) { title_input_up();   return; }
    if (combat_is_active()) { combat_input_attack(); return; }
    if (dialog_is_open()) { dialog_input_prev(); return; }
    if (menu_is_open())   { menu_input_up();     return; }
    if (shop_is_open())   { shop_input_up();     return; }
    if (minimap_is_open()) { minimap_input_up(); return; }
    
    int dx = 0, dy = 0;
    if      (g_player.facing == FACING_NORTH) dy = -1;
    else if (g_player.facing == FACING_EAST)  dx =  1;
    else if (g_player.facing == FACING_SOUTH) dy =  1;
    else if (g_player.facing == FACING_WEST)  dx = -1;

    int nx = g_player.x + dx;
    int ny = g_player.y + dy;

    // Pushable block in front: try to shove it forward (Grimrock-style). On
    // success the player advances into the vacated tile.
    if (map_get_decor(nx, ny) == DECOR_BLOCK) {
        if (puzzle_try_push(g_player.x, g_player.y, g_player.facing)) {
            g_player.x = nx;
            g_player.y = ny;
            puzzle_recompute_plates(nx, ny);
            event_check_tile(nx, ny);
            if (!combat_is_active() && !dialog_is_open()) {
                encounter_check();
            }
            save_write();
        }
        layer_mark_dirty(s_canvas);
        return;
    }

    if (map_is_walkable(nx, ny)) {
        g_player.x = nx;
        g_player.y = ny;
        puzzle_recompute_plates(nx, ny);
        event_check_tile(nx, ny);
        if (!combat_is_active() && !dialog_is_open()) {      // Dont roll if in combat or in dialog
            encounter_check();
        }
        save_write();      
        layer_mark_dirty(s_canvas);
    } else {
        // movement blocked — check if the tile in front has a forward event
        event_check_forward(g_player.x, g_player.y, g_player.facing);
        layer_mark_dirty(s_canvas);
    }
}

static void walkBack(){
    int dx = 0, dy = 0;
    if      (g_player.facing == FACING_NORTH) dy =  1;
    else if (g_player.facing == FACING_EAST)  dx = -1;
    else if (g_player.facing == FACING_SOUTH) dy = -1;
    else if (g_player.facing == FACING_WEST)  dx =  1;

    int nx = g_player.x + dx;
    int ny = g_player.y + dy;

    if (map_is_walkable(nx, ny)) {
        g_player.x = nx;
        g_player.y = ny;
        puzzle_recompute_plates(nx, ny);
        event_check_tile(nx, ny);
        if (!combat_is_active() && !dialog_is_open() && !shop_is_open()) {
            encounter_check();
        }
        save_write();
        layer_mark_dirty(s_canvas);
    }
}

static void down_click(ClickRecognizerRef recognizer, void *ctx) {

    if (title_is_active()) { title_input_down();  return; }
    if (combat_is_active()) {
        // DOWN: cast Signal spell if known, else potion
        if (g_player.spellbook >= SPELL_HEAL) combat_input_spell();
        else combat_input_item(0);
        return;
    }
    if (dialog_is_open()) { dialog_input_next(); return; }
    if (menu_is_open())   { menu_input_down();   return; }
    if (shop_is_open())   { shop_input_down();   return; }
    if (minimap_is_open()) { minimap_close(); return; }

    // Exploration: turn left (mirror of SELECT's turn right). The items
    // menu moved to hold-DOWN.
    g_player.facing = (g_player.facing + 3) % 4;
    layer_mark_dirty(s_canvas);
}

// Hold DOWN opens the items/stats menu during exploration.
static void down_long_click(ClickRecognizerRef recognizer, void *ctx) {
    if (title_is_active() || combat_is_active() || dialog_is_open() ||
        menu_is_open()    || shop_is_open()     || minimap_is_open()) return;
    menu_open();
}

static void select_click(ClickRecognizerRef recognizer, void *ctx) {
    if (title_is_active()) { title_input_select(); return; }

    if (combat_is_active()) { combat_input_select(); return; }
    if (dialog_is_open()) { dialog_input_next(); return; }
    if (menu_is_open())   { menu_input_select();  return; }
    if (shop_is_open())   { shop_input_select();  return; }
    if (minimap_is_open()) { minimap_close(); return; }

    // Turn right
    g_player.facing = (g_player.facing + 1) % 4;
    layer_mark_dirty(s_canvas);
}

// Long-press SELECT toggles the Grimrock-style auto-map during exploration.
static void select_long_click(ClickRecognizerRef recognizer, void *ctx) {
    if (title_is_active() || combat_is_active() ||
        dialog_is_open()  || menu_is_open() || shop_is_open()) return;
    minimap_toggle();
    layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef recognizer, void *ctx) {
    if (title_is_active()) return;  // back does nothing on title
    if (combat_is_active()) { combat_input_run(); return; }
    if (dialog_is_open()) { dialog_input_exit(); return; }
    if (menu_is_open())   { menu_input_back();   return; }
    if (shop_is_open())   { shop_input_back();   return; }
    if (minimap_is_open()) { minimap_close(); return; }
    // Turn left
    g_player.facing = (g_player.facing + 3) % 4;  // +3 mod 4 = turn left
    layer_mark_dirty(s_canvas);
}

static void click_config_provider(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP,     up_click);
    window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_click_subscribe(BUTTON_ID_BACK,   back_click);
    // Hold SELECT to open the auto-map; hold DOWN to open the items menu.
    window_long_click_subscribe(BUTTON_ID_SELECT, 400, select_long_click, NULL);
    window_long_click_subscribe(BUTTON_ID_DOWN,   400, down_long_click,   NULL);
}

void touch_handle(int x, int y) {
    GRect bounds  = layer_get_bounds(s_canvas);
    int   w = bounds.size.w;
    int   h = bounds.size.h;

    if (title_is_active()) {
        // simple top/bottom/center for title
        TouchZone z = touch_classify_movement(x, y, w, h);
        if (z == TOUCH_UP)     title_input_up();
        if (z == TOUCH_DOWN)   title_input_down();
        if (z == TOUCH_CENTER) title_input_select();
        if (z == TOUCH_RIGHT)  title_input_select();
        return;
    }

    if (dialog_is_open()) {
        dialog_input_next();  // any tap advances
        return;
    }

    if (menu_is_open()) {
        // check row taps first
        int count = menu_row_zone_count();
        int rh    = menu_row_zone_h() / 2;
        for (int i = 0; i < count; i++) {
            int ry = menu_row_zone_y(i);
            if (y >= ry - rh && y <= ry + rh) {
                // tapped this row — select it if already selected, else move to it
                if (i + menu_get_scroll() == menu_get_selected()) {
                    menu_input_select();
                } else {
                    // jump selection to tapped row
                    menu_touch_select_row(i);
                }
                return;
            }
        }
        // top/bottom for scroll
        if (y < h * 35/100 && x > w * 33/100) { menu_input_up();   return; }
        if (y > h * 65/100 && x > w * 33/100) { menu_input_down(); return; }
        // left edge = exit
        if (x < w * 33/100) { menu_input_back(); return; }
        return;
    }

    if (shop_is_open()) {
        if (y < h * 35/100) { shop_input_up(); return; }
        if (y > h * 65/100) { shop_input_down(); return; }
        if (x < w * 33/100) { shop_input_back(); return; }
        shop_input_select();
        return;
    }

    if (combat_is_active()) {
        CombatPhase phase = combat_get_phase();
        if (phase == COMBAT_PHASE_VICTORY ||
            phase == COMBAT_PHASE_DEFEAT) {
            // Same path as Select — runs victory event / respawn.
            combat_input_select();
            return;
        }
        if (phase == COMBAT_PHASE_INPUT) {
            // check icon tap zones
            int count = combat_icon_zone_count();
            int iy    = combat_icon_zone_y();
            int ir    = combat_icon_zone_r();
            for (int i = 0; i < count; i++) {
                int ix = combat_icon_zone_x(i);
                int dx = x - ix, dy = y - iy;
                if (dx*dx + dy*dy < ir*ir) {
                    // Icons: [Attack] [Spell|Potion?] [Run?]
                    // When can_run is false, the last icon is spell/potion —
                    // never treat it as Run (boss fights).
                    if (i == 0) {
                        combat_input_attack();
                    } else if (combat_can_run() && i == count - 1) {
                        combat_input_run();
                    } else {
                        combat_input_spell();  // routes to potion via down_mode
                    }
                    return;
                }
            }
            // tapping anywhere else on combat screen = attack
            if (y < h/2) combat_input_attack();
        }
        return;
    }

    // exploration movement
    TouchZone z = touch_classify_movement(x, y, w, h);
    switch(z) {
        case TOUCH_UP:     up_click(NULL, NULL);     break;
        case TOUCH_DOWN:   walkBack();   break;
        case TOUCH_LEFT:   back_click(NULL, NULL);   break;
        case TOUCH_RIGHT:  select_click(NULL, NULL); break;
        case TOUCH_CENTER: menu_open();
                           layer_mark_dirty(s_canvas);
                           break;
        default: break;
    }
}

#if defined(PBL_TOUCH)
static void touch_service_handler(const TouchEvent *event, void *context) {
    if (!event) return;
    // only act on initial touchdown — ignore position updates and liftoff
    if (event->type == TouchEvent_Liftoff) {
        touch_handle((int)event->x, (int)event->y);
    }
}
#endif



static void window_load(Window *window) {
    Layer *root  = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    s_atlas  = gbitmap_create_with_resource(RESOURCE_ID_ATLAS);
    s_bitfont = gbitmap_create_with_resource(RESOURCE_ID_BITFONT);  // add this
    s_icon_atlas = gbitmap_create_with_resource(RESOURCE_ID_ICONS);
    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);
    
    map_init_for(0);
    event_load_map(0);
    encounter_init(0);  // 0 = prison map
    player_init();

    event_init(s_canvas);  // add this
    dialog_init(s_canvas);
    menu_init(s_canvas, s_icon_atlas);  // needs its own GBitmap — see below
    shop_init(s_canvas, s_icon_atlas);
    combat_init(s_canvas);
    title_init(s_canvas, NULL);
    minimap_init(s_canvas);

    window_set_click_config_provider(s_window, click_config_provider);

    #if defined(PBL_TOUCH)
        if (touch_service_is_enabled()) {
            touch_service_subscribe(touch_service_handler, NULL);
        }
    #endif
}

static void window_unload(Window *window) {
    // Persist progress on exit so shop buys / combat loot / position survive.
    if (!title_is_active()) {
        save_write();
    }
    layer_destroy(s_canvas);  s_canvas = NULL;
    event_cleanup();
    gbitmap_destroy(s_atlas); s_atlas  = NULL;
    gbitmap_destroy(s_bitfont); s_bitfont = NULL;  // add this
    gbitmap_destroy(s_icon_atlas); s_icon_atlas = NULL;
    monster_bitmap_unload();
    
    #if defined(PBL_TOUCH)
        touch_service_unsubscribe();
    #endif
}

int main(void) {
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
    app_event_loop();
    window_destroy(s_window);
    return 0;
}