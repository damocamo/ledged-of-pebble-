#include "pebble.h"
#include "menu.h"
#include "bitfont.h"
#include "player.h"
#include "event.h"
#include "magic.h"
#include "save.h"
#include "minimap.h"


// ---- Item registry ------------------------------------------------------
// Add items here — quantity starts at 0 (not owned)

#define MAX_OWNED 4

static int8_t s_owned_list[MAX_OWNED];  // indices into s_items[]
static int    s_owned_count = 0;

static const ItemDef s_items[] = {
    { "HP POTION",     ITEM_TYPE_CONSUMABLE,  9,  -1, -1 },
    { "KEY",           ITEM_TYPE_KEY,        11,  -1, -1 },
    { "MAP REVEAL",    ITEM_TYPE_MAP,         2,  -1, -1 },
    { "SIGNAL REST",   ITEM_TYPE_REST,        3,  -1, -1 },
};

#define ITEM_COUNT ((int)ARRAY_LENGTH(s_items))

// ---- Constants ----------------------------------------------------------
#define ICON_SIZE       16    // source icon size in atlas
#define ICON_SCALE       3    // draw at 3x = 48px
#define ICON_DRAW_SIZE  (ICON_SIZE * ICON_SCALE)
#define ROW_H           (ICON_DRAW_SIZE + 9)    // px per row
#define VISIBLE_ROWS     3    // how many rows fit on screen


static int s_row_zones[VISIBLE_ROWS];   // y center of each visible row
static int s_row_zone_count = 0;

// ---- State --------------------------------------------------------------
static bool      s_open        = false;
static int       s_selected    = 0;     // currently selected index
static int       s_scroll_y    = 0;     // current scroll offset in px
static Layer    *s_canvas_ref  = NULL;
static GBitmap  *s_icon_atlas  = NULL;

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
        case ITEM_TYPE_KEY: {
            // Keys open alcoves by walking into them (FORWARD trigger).
            // Using from the menu tries the same facing check; if nothing
            // happens, hint the player.
            int before = player_item_count(item_idx);
            event_check_forward(g_player.x, g_player.y, g_player.facing);
            if (player_item_count(item_idx) == before) {
                snprintf(g_magic_msg, sizeof(g_magic_msg), "FACE THE LOCK");
            }
            break;
        }
        case ITEM_TYPE_MAP:
            // Works on whatever floor you are standing on when used.
            minimap_reveal_all(g_player.map_id);
            player_take_item(item_idx, 1);
            snprintf(g_magic_msg, sizeof(g_magic_msg), "FLOOR MAPPED");
            save_write();
            break;
        case ITEM_TYPE_REST:
            g_player.hp = g_player.max_hp;
            g_player.mp = g_player.max_mp;
            player_set_respawn();
            player_take_item(item_idx, 1);
            snprintf(g_magic_msg, sizeof(g_magic_msg), "RESTORED");
            save_write();
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

// Stats column font: scale 2 (16px lines) so every entry fits a 200px-wide
// screen. Scale 3 lines (24px) with the old +-90/+-104 offsets ran long names
// and the MP block off the screen edges.
#define STAT_SCALE  2
#define STAT_LH     (8 * STAT_SCALE + 3)   // line height + spacing
#define STAT_X      6                      // left margin
#define STAT_GAP    4                      // extra gap between stat groups

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
        bitfont_render_scaled(fb, "NO ITEMS", fb_bounds.size.w - STAT_X,
                              list_top, JUSTIFY_RIGHT, STAT_SCALE);
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

            // selection indicator next to the icon; name shown at the bottom
            if (selected) {
                bitfont_render_scaled(fb, ">", icon_x - 16,
                                      row_y + ICON_DRAW_SIZE / 2 - 8,
                                      JUSTIFY_LEFT, STAT_SCALE);
            }

            draw_icon(fb, item->icon_slot, icon_x, row_y);

            // quantity
            if (quantity > 0) {
                static char qty_buf[5];
                snprintf(qty_buf, sizeof(qty_buf), "%d", (int8_t)quantity);
                bitfont_render_scaled(fb, qty_buf, fb_bounds.size.w - STAT_X,
                                      row_y + ICON_DRAW_SIZE - 16,
                                      JUSTIFY_RIGHT, STAT_SCALE);
            }
        }

        // scroll indicators
        if (s_scroll_y > 0)
            bitfont_render_scaled(fb, "^", icon_x + ICON_DRAW_SIZE/2,
                                  list_top - 18, JUSTIFY_CENTERV, STAT_SCALE);
        if (s_scroll_y + VISIBLE_ROWS < s_owned_count)
            bitfont_render_scaled(fb, "v", icon_x + ICON_DRAW_SIZE/2,
                                  list_top + VISIBLE_ROWS * ROW_H - 14,
                                  JUSTIFY_CENTERV, STAT_SCALE);
    }

    // ---- Stats column (left) — single fixed-width column, top to bottom ----
    int sy = 8;
    static char stat_buf[24];

    bitfont_render_scaled(fb, "WEAPON:", STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH;
    bitfont_render_scaled(fb, player_get_weapon()->name, STAT_X, sy,
                          JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH + STAT_GAP;

    bitfont_render_scaled(fb, "ARMOR:", STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH;
    bitfont_render_scaled(fb, player_get_armor()->name, STAT_X, sy,
                          JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH + STAT_GAP;

    snprintf(stat_buf, sizeof(stat_buf), "HP %d/%d", g_player.hp, g_player.max_hp);
    bitfont_render_scaled(fb, stat_buf, STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH;

    snprintf(stat_buf, sizeof(stat_buf), "MP %d/%d", g_player.mp, g_player.max_mp);
    bitfont_render_scaled(fb, stat_buf, STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH;

    snprintf(stat_buf, sizeof(stat_buf), "GOLD %d", player_get_gold());
    bitfont_render_scaled(fb, stat_buf, STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    sy += STAT_LH + STAT_GAP;

    if (g_player.spellbook >= SPELL_HEAL) {
        snprintf(stat_buf, sizeof(stat_buf), "SP:%s",
                 player_spell_name(magic_selected_spell()));
        bitfont_render_scaled(fb, stat_buf, STAT_X, sy, JUSTIFY_LEFT, STAT_SCALE);
    }

    // ---- Bottom line: magic feedback, or the selected item's name ----------
    int bottom_y = fb_bounds.size.h - STAT_LH - 4;
    if (g_magic_msg[0]) {
        bitfont_render_scaled(fb, g_magic_msg, cx, bottom_y,
                              JUSTIFY_CENTERV, STAT_SCALE);
    } else if (s_owned_count > 0 && s_selected < s_owned_count) {
        bitfont_render_scaled(fb, s_items[s_owned_list[s_selected]].name,
                              cx, bottom_y, JUSTIFY_CENTERV, STAT_SCALE);
    }
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