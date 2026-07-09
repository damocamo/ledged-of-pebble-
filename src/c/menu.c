#include "pebble.h"
#include "menu.h"
#include "bitfont.h"
#include "player.h"
#include "event.h"
#include "magic.h"
#include "save.h"


// ---- Item registry ------------------------------------------------------
// Add items here — quantity starts at 0 (not owned)

#define MAX_OWNED 4

static int8_t s_owned_list[MAX_OWNED];  // indices into s_items[]
static int    s_owned_count = 0;

static const ItemDef s_items[] = {
    { "HP POTION",     ITEM_TYPE_CONSUMABLE,  9,  -1, -1 },
    { "KEY",           ITEM_TYPE_KEY,        11,  -1, -1 },
};

#define ITEM_COUNT ((int)ARRAY_LENGTH(s_items))

// ---- Constants ----------------------------------------------------------
#define ICON_SIZE       16    // source icon size in atlas
#define ICON_SCALE       3    // draw at 3x = 48px
#define ICON_DRAW_SIZE  (ICON_SIZE * ICON_SCALE)
#define ROW_H           (ICON_DRAW_SIZE + 9)    // px per row
#define VISIBLE_ROWS     3    // how many rows fit on screen
#define SCROLL_SPEED     4    // px per tick
#define SCROLL_MS       16    // ~60fps tick


static int s_row_zones[VISIBLE_ROWS];   // y center of each visible row
static int s_row_zone_count = 0;
static int s_row_zone_h     = ROW_H;

// ---- State --------------------------------------------------------------
static bool      s_open        = false;
static int       s_selected    = 0;     // currently selected index
static int       s_scroll_y    = 0;     // current scroll offset in px
static int       s_target_y    = 0;     // target scroll offset in px
static Layer    *s_canvas_ref  = NULL;
static GBitmap  *s_icon_atlas  = NULL;
static AppTimer *s_scroll_timer = NULL;

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// ---- Scroll timer -------------------------------------------------------
static void scroll_tick(void *context) {
    // s_scroll_timer = NULL;
    // if (s_scroll_y == s_target_y) return;

    // int diff = s_target_y - s_scroll_y;
    // int step = diff > 0 ? MIN(SCROLL_SPEED, diff)
    //                     : MAX(-SCROLL_SPEED, diff);
    // s_scroll_y += step;

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);

    // // keep ticking until we reach target
    // if (s_scroll_y != s_target_y) {
    //     s_scroll_timer = app_timer_register(SCROLL_MS, scroll_tick, NULL);
    // }
}

static void start_scroll(void) {
    // target keeps selected item centered in visible area
    s_target_y = s_selected * ROW_H - (VISIBLE_ROWS / 2) * ROW_H;
    // clamp
    int max_scroll = (ITEM_COUNT - VISIBLE_ROWS) * ROW_H;
    if (s_target_y < 0)          s_target_y = 0;
    if (s_target_y > max_scroll) s_target_y = max_scroll;

    if (s_scroll_timer) app_timer_cancel(s_scroll_timer);
    s_scroll_timer = app_timer_register(SCROLL_MS, scroll_tick, NULL);
}

static void build_owned_list(void) {
    s_owned_count = 0;
    for (int i = 0; i < ITEM_COUNT && s_owned_count < MAX_OWNED; i++) {
        if (player_has_item(i)) {
            s_owned_list[s_owned_count++] = i;
        }
    }
}

// ---- Public API ---------------------------------------------------------
void menu_init(Layer *canvas, GBitmap *icon_atlas) {
    s_canvas_ref = canvas;
    s_icon_atlas = icon_atlas;
}

bool menu_is_open(void) { return s_open; }

void menu_open(void) {
    s_open      = true;
    s_scroll_y  = 0;
    g_magic_msg[0] = '\0';
    magic_reset_selection();
    build_owned_list();
    // clamp selected in case inventory changed since last open
    if (s_selected >= s_owned_count) s_selected = 0;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void menu_close(void) {
    s_open = false;
    if (s_scroll_timer) {
        app_timer_cancel(s_scroll_timer);
        s_scroll_timer = NULL;
    }
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void menu_input_up(void) {
    if (!s_open) return;
    if (s_selected > 0) {
        s_selected--;
        if (s_selected < s_scroll_y)
            s_scroll_y = s_selected;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    // At top: cycle Signal spell
    if (g_player.spellbook >= SPELL_HEAL) {
        magic_cycle_spell();
        snprintf(g_magic_msg, sizeof(g_magic_msg), "SPELL:%s",
                 player_spell_name(magic_selected_spell()));
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    }
}

void menu_input_down(void) {
    if (!s_open) return;
    if (s_selected < s_owned_count - 1) {
        s_selected++;
        if (s_selected >= s_scroll_y + VISIBLE_ROWS)
            s_scroll_y = s_selected - VISIBLE_ROWS + 1;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    // At bottom / empty: cast selected Signal spell
    if (g_player.spellbook >= SPELL_HEAL) {
        SpellId sp = magic_selected_spell();
        bool ok = false;
        if (sp == SPELL_HEAL) ok = magic_explore_heal();
        else if (sp == SPELL_PURGE) ok = magic_explore_purge();
        else if (sp == SPELL_DECODE) ok = magic_explore_decode();
        if (ok) save_write();
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
    }
}

void menu_input_select(void) {
    if (!s_open) return;

    // With no items: Select casts / cycles Signal Magic if known.
    if (s_owned_count == 0) {
        if (g_player.spellbook >= SPELL_HEAL) {
            SpellId sp = magic_selected_spell();
            bool ok = false;
            if (sp == SPELL_HEAL) ok = magic_explore_heal();
            else if (sp == SPELL_PURGE) ok = magic_explore_purge();
            else if (sp == SPELL_DECODE) ok = magic_explore_decode();
            if (ok) save_write();
            magic_cycle_spell();
        }
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    if (s_selected >= s_owned_count) return;

    int item_idx       = s_owned_list[s_selected];
    const ItemDef *item = &s_items[item_idx];

    switch (item->type) {
        case ITEM_TYPE_CONSUMABLE:
            player_heal(10);
            player_take_item(item_idx, 1);
            break;
        case ITEM_TYPE_KEY:
            event_check_item(g_player.x, g_player.y, g_player.facing, -1);
            break;
    }

    // rebuild list in case item was depleted
    build_owned_list();
    // clamp selection if we used the last of something
    if (s_selected >= s_owned_count && s_owned_count > 0)
        s_selected = s_owned_count - 1;
    if (s_scroll_y > s_selected)
        s_scroll_y = s_selected;

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void menu_input_back(void) {
    menu_close();
}

// ---- Give item (call from event system) ---------------------------------
// void menu_give_item(int item_index, int quantity) {
//     if (item_index < 0 || item_index >= ITEM_COUNT) return;
//     s_items[item_index].quantity += quantity;
// }

// ---- Draw ---------------------------------------------------------------
static void draw_icon(GBitmap *fb, int icon_slot, int dest_x, int dest_y) {
    if (!s_icon_atlas) return;

    uint8_t *data    = gbitmap_get_data(s_icon_atlas);
    int      stride  = gbitmap_get_bytes_per_row(s_icon_atlas);
    GColor  *palette = gbitmap_get_palette(s_icon_atlas);
    GRect    fb_bounds = gbitmap_get_bounds(fb);

    int src_x = icon_slot * ICON_SIZE;

    for (int row = 0; row < ICON_SIZE; row++) {
        for (int dy = 0; dy < ICON_SCALE; dy++) {
            int fb_y = dest_y + row * ICON_SCALE + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);

            for (int col = 0; col < ICON_SIZE; col++) {
                int map_x  = src_x + col;
                uint8_t byte  = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F
                                                  : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;  // transparent

                for (int dx = 0; dx < ICON_SCALE; dx++) {
                    int fb_x = dest_x + col * ICON_SCALE + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

void menu_draw(GBitmap *fb) {
    if (!s_open) return;

    // rebuild in case inventory changed while open
    build_owned_list();

    GRect fb_bounds = gbitmap_get_bounds(fb);
    int cx = fb_bounds.size.w / 2;
    int cy = fb_bounds.size.h / 2;
    int list_top = cy - ICON_DRAW_SIZE * 3 / 2;
    int icon_x = cx + 40;

    s_row_zone_count = 0;  // reset

    if (s_owned_count == 0) {
        bitfont_render(fb, "NO ITEMS", cx, list_top - 27 - 9, JUSTIFY_CENTERV);
    } else {
        // draw only the visible window of rows
        for (int vis = 0; vis < VISIBLE_ROWS; vis++) {

            int list_idx = s_scroll_y + vis;
            if (list_idx >= s_owned_count) break;
            int row_y = list_top + vis * ROW_H;
            s_row_zones[s_row_zone_count++] = row_y + ROW_H / 2;

            int item_idx        = s_owned_list[list_idx];
            const ItemDef *item = &s_items[item_idx];
            int quantity        = player_item_count(item_idx);
            bool selected       = (list_idx == s_selected);


            // selection indicator and item name at top
            if (selected) {
                bitfont_render(fb, ">", cx + 25, row_y + 10, JUSTIFY_LEFT);
                bitfont_render(fb, item->name, cx, list_top - 27 - 9, JUSTIFY_CENTERV);
            }

            // icon
            
            draw_icon(fb, item->icon_slot, icon_x, row_y);

            // quantity
            if (quantity > 0) {
                static char qty_buf[5];
                snprintf(qty_buf, sizeof(qty_buf), "%d", (int8_t)quantity);
                bitfont_render(fb, qty_buf, icon_x + ICON_DRAW_SIZE, row_y + ICON_DRAW_SIZE-18, JUSTIFY_RIGHT);
            }
        }

        // scroll indicators
        if (s_scroll_y > 0)
            bitfont_render(fb, "^", icon_x + ICON_DRAW_SIZE/2, list_top - 12, JUSTIFY_CENTERV);
        if (s_scroll_y + VISIBLE_ROWS < s_owned_count)
            bitfont_render(fb, "v", icon_x + ICON_DRAW_SIZE/2,
                           list_top + VISIBLE_ROWS * ROW_H - 12, JUSTIFY_CENTERV);
    }

    static char equip_buf[24];
    snprintf(equip_buf, sizeof(equip_buf), "WEAPON:\n%s",
            player_get_weapon()->name);
    bitfont_render(fb, equip_buf, cx - 90, cy - 27*3 + 9, JUSTIFY_LEFT);

    snprintf(equip_buf, sizeof(equip_buf), "ARMOR:\n%s",
            player_get_armor()->name);
    bitfont_render(fb, equip_buf, cx - 90, cy - 24/2 + 3, JUSTIFY_LEFT);

    // HP / MP display
    static char hp_buf[20];
    snprintf(hp_buf, sizeof(hp_buf), "HP:\n%d/%d", g_player.hp, g_player.max_hp);
    bitfont_render(fb, hp_buf, cx - 90, cy + 104 - 27 - 24, JUSTIFY_LEFT);

    snprintf(hp_buf, sizeof(hp_buf), "MP:\n%d/%d", g_player.mp, g_player.max_mp);
    bitfont_render(fb, hp_buf, cx - 90, cy + 104 - 27, JUSTIFY_LEFT);

    static char gold_buf[20];
    snprintf(gold_buf, sizeof(gold_buf), "GOLD:\n%d", player_get_gold());
    bitfont_render(fb, gold_buf, cx + 90, cy + 104 - 27 - 24, JUSTIFY_RIGHT);

    if (g_player.spellbook >= SPELL_HEAL) {
        static char sp_buf[24];
        snprintf(sp_buf, sizeof(sp_buf), "SPELL:\n%s",
                 player_spell_name(magic_selected_spell()));
        bitfont_render(fb, sp_buf, cx + 90, cy - 27*3 + 9, JUSTIFY_RIGHT);
    }
    if (g_magic_msg[0]) {
        bitfont_render(fb, g_magic_msg, cx, cy + 104 - 48, JUSTIFY_CENTER);
    }

    // hints
    //bitfont_render(fb, "SL:USE\nBK:EXT", cx + 90, cy + 104 - 27 - 24,  JUSTIFY_RIGHT);
}

// Expose for touch.c
// Add accessor functions:
int menu_row_zone_count(void) { return s_row_zone_count; }
int menu_row_zone_y(int i)    { return s_row_zones[i]; }
int menu_row_zone_h(void)     { return ROW_H; }
int menu_get_selected(void)   { return s_selected; }
int menu_get_scroll(void)     { return s_scroll_y; }

void menu_touch_select_row(int vis_index) {
    s_selected = s_scroll_y + vis_index;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}