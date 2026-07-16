#include "pebble.h"
#include "shop.h"
#include "bitfont.h"
#include "player.h"
#include "event.h"
#include "map.h"
#include "save.h"
#include "menu.h"

// Shared merchant stock + per-floor gossip. Armor/weapons one tier at a time.

#define SHOP_ICON_SIZE  16
#define SHOP_ICON_SCALE  3
#define SHOP_ICON_DRAW  (SHOP_ICON_SIZE * SHOP_ICON_SCALE)
#define SHOP_ROW_H      (SHOP_ICON_DRAW + 4)
#define SHOP_VISIBLE     3

typedef enum {
    SHOP_POTION = 0,
    SHOP_MAP    = 1,
    SHOP_ARMOR  = 2,
    SHOP_WEAPON = 3,
    SHOP_SPELL  = 4,
    SHOP_REST   = 5,
} ShopKind;

typedef struct {
    const char *name;
    ShopKind    kind;
    uint8_t     icon_slot;
    int16_t     price;
    uint8_t     value;  // armor_id / weapon_id / spell_id
} ShopItem;

static const ShopItem s_stock[] = {
    { "HP POTION",    SHOP_POTION,  9,  5, 0 },
    { "MAP REVEAL",   SHOP_MAP,     2, 10, 0 },
    { "SIGNAL REST",  SHOP_REST,    3, 10, 0 },
    { "STICK",        SHOP_WEAPON, 14,  8, WEAPON_STICK },
    { "DAGGER",       SHOP_WEAPON, 18, 25, WEAPON_DAGGER },
    { "SIGNAL BLADE", SHOP_WEAPON, 18, 55, WEAPON_SPIKE },
    { "CLOAK",        SHOP_ARMOR,  15, 18, ARMOR_CLOAK },
    { "VEST",         SHOP_ARMOR,  15, 30, ARMOR_VEST },
    { "PLATE",        SHOP_ARMOR,  15, 45, ARMOR_PLATE },
    { "SIGNAL MAIL",  SHOP_ARMOR,  15, 65, ARMOR_SIGNAL },
    { "SPELL:PURGE",  SHOP_SPELL,  12, 40, SPELL_PURGE },
    { "SPELL:DECODE", SHOP_SPELL,  12, 80, SPELL_DECODE },
};
#define STOCK_COUNT ((int)ARRAY_LENGTH(s_stock))

static bool      s_open = false;
static int       s_selected = 0;
static int       s_scroll = 0;
static Layer    *s_canvas_ref = NULL;
static GBitmap  *s_icon_atlas = NULL;
static char      s_status[16] = "";
static int       s_gossip_page = 0;

extern GBitmap *s_monsters;
void monster_bitmap_load(int slot);   // main.c — loads one 58x63 slice
void monster_bitmap_unload(void);

// Per-floor merchant gossip (2 lines each, cycled on open).
static const char *const s_gossip[][2] = {
    /* map 0 unused */ { "", "" },
    /* map 1 (L2) */ { "MAPS WORK ON", "ANY FLOOR." },
    /* map 2 */      { "WATCH THE", "STATIC." },
    /* map 3 */      { "GEMS HIDE", "IN CHESTS." },
    /* map 4 (L5) */ { "THE KEEPER", "WAITS AHEAD." },
    /* map 5 */      { "PURGE THE", "SIGNAL PILES." },
    /* map 6 (L7) */ { "DECODE OPENS", "LOCKED DOORS." },
    /* map 7 */      { "REST WHERE", "YOU NEED IT." },
    /* map 8 */      { "THE CORE", "HUMS LOUD." },
    /* map 9 (L10)*/ { "THE ARCHITECT", "BUILT THIS." },
};

static void set_status(const char *msg) {
    snprintf(s_status, sizeof(s_status), "%s", msg);
}

static bool item_available(const ShopItem *it) {
    switch (it->kind) {
        case SHOP_POTION: return true;
        // Map / rest are carried items now: one of each at a time.
        case SHOP_MAP:    return !player_has_item(ITEM_SLOT_MAP);
        case SHOP_REST:   return !player_has_item(ITEM_SLOT_REST);
        case SHOP_ARMOR:  return g_player.armor + 1 == it->value;
        case SHOP_WEAPON: return g_player.weapon < it->value;  // any better tier (skip Stick OK)
        case SHOP_SPELL:
            // Must own previous spell tier; don't re-sell known.
            if (g_player.spellbook >= it->value) return false;
            return g_player.spellbook + 1 == it->value;
    }
    return false;
}

static int visible_count(void) {
    int n = 0;
    for (int i = 0; i < STOCK_COUNT; i++) {
        if (item_available(&s_stock[i])) n++;
    }
    return n;
}

static int stock_index_for_row(int row) {
    int n = 0;
    for (int i = 0; i < STOCK_COUNT; i++) {
        if (!item_available(&s_stock[i])) continue;
        if (n == row) return i;
        n++;
    }
    return -1;
}

static void draw_icon(GBitmap *fb, int icon_slot, int dest_x, int dest_y) {
    if (!s_icon_atlas) return;
    uint8_t *data = gbitmap_get_data(s_icon_atlas);
    int stride = gbitmap_get_bytes_per_row(s_icon_atlas);
    GColor *palette = gbitmap_get_palette(s_icon_atlas);
    GRect fb_bounds = gbitmap_get_bounds(fb);
    int src_x0 = icon_slot * SHOP_ICON_SIZE;

    for (int row = 0; row < SHOP_ICON_SIZE; row++) {
        for (int dy = 0; dy < SHOP_ICON_SCALE; dy++) {
            int fb_y = dest_y + row * SHOP_ICON_SCALE + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);
            for (int col = 0; col < SHOP_ICON_SIZE; col++) {
                int map_x = src_x0 + col;
                uint8_t byte = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;
                for (int dx = 0; dx < SHOP_ICON_SCALE; dx++) {
                    int fb_x = dest_x + col * SHOP_ICON_SCALE + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

#define MERCHANT_SLOT 10
#define MERCHANT_W 58
#define MERCHANT_H 63

static void draw_merchant_portrait(GBitmap *fb, int dest_x, int dest_y) {
    if (!s_monsters) return;
    uint8_t *data = gbitmap_get_data(s_monsters);
    int stride = gbitmap_get_bytes_per_row(s_monsters);
    GColor *palette = gbitmap_get_palette(s_monsters);
    GRect fb_bounds = gbitmap_get_bounds(fb);
    int sprite_x = 0;  // s_monsters holds just the merchant slice
    const int scale = 2;

    for (int row = 0; row < MERCHANT_H; row++) {
        for (int dy = 0; dy < scale; dy++) {
            int fb_y = dest_y + row * scale + dy;
            if (fb_y < 0 || fb_y >= fb_bounds.size.h) continue;
            GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, fb_y);
            for (int col = 0; col < MERCHANT_W; col++) {
                int map_x = sprite_x + col;
                uint8_t byte = data[row * stride + (map_x / 2)];
                uint8_t index = (map_x % 2 == 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
                GColor col_px = palette[index];
                if (!col_px.a) continue;
                for (int dx = 0; dx < scale; dx++) {
                    int fb_x = dest_x + col * scale + dx;
                    if (fb_x >= info.min_x && fb_x <= info.max_x)
                        info.data[fb_x] = col_px.argb;
                }
            }
        }
    }
}

void shop_init(Layer *canvas, GBitmap *icon_atlas) {
    s_canvas_ref = canvas;
    s_icon_atlas = icon_atlas;
}

bool shop_is_open(void) { return s_open; }

void shop_open(void) {
    monster_bitmap_load(MERCHANT_SLOT);
    s_open = true;
    s_selected = 0;
    s_scroll = 0;
    s_status[0] = '\0';
    s_gossip_page = 0;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void shop_close(void) {
    monster_bitmap_unload();
    s_open = false;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void shop_input_up(void) {
    if (s_selected > 0) {
        s_selected--;
        if (s_selected < s_scroll) s_scroll = s_selected;
    } else {
        s_gossip_page ^= 1;
    }
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void shop_input_down(void) {
    int n = visible_count();
    if (s_selected < n - 1) {
        s_selected++;
        if (s_selected >= s_scroll + SHOP_VISIBLE)
            s_scroll = s_selected - SHOP_VISIBLE + 1;
    } else {
        s_gossip_page ^= 1;
    }
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void shop_input_back(void) {
    shop_close();
}

void shop_input_select(void) {
    int si = stock_index_for_row(s_selected);
    if (si < 0) {
        set_status("SOLD OUT");
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    const ShopItem *it = &s_stock[si];
    if (!item_available(it)) {
        set_status("UNAVAILABLE");
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
    if (!player_spend_gold(it->price)) {
        set_status("NOT ENOUGH");
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }

    switch (it->kind) {
        case SHOP_POTION:
            player_give_item(ITEM_SLOT_POTION, 1);
            set_status("POTION +1");
            break;
        case SHOP_MAP:
            // Goes into the pack — use it from the menu on any floor.
            player_give_item(ITEM_SLOT_MAP, 1);
            set_status("MAP PACKED");
            if (s_selected >= visible_count() && s_selected > 0) s_selected--;
            break;
        case SHOP_REST:
            player_give_item(ITEM_SLOT_REST, 1);
            set_status("REST PACKED");
            if (s_selected >= visible_count() && s_selected > 0) s_selected--;
            break;
        case SHOP_ARMOR:
            player_set_armor((ArmorId)it->value);
            set_status("EQUIPPED!");
            if (s_selected >= visible_count() && s_selected > 0) s_selected--;
            break;
        case SHOP_WEAPON:
            player_set_weapon((WeaponId)it->value);
            set_status("EQUIPPED!");
            if (s_selected >= visible_count() && s_selected > 0) s_selected--;
            break;
        case SHOP_SPELL:
            player_set_spellbook((SpellId)it->value);
            set_status("LEARNED!");
            if (s_selected >= visible_count() && s_selected > 0) s_selected--;
            break;
    }
    save_write();
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

// Shop text renders at scale 2 (16px lines): scale-3 titles, gossip, and
// item names overflowed the 200px screen and collided with each other.
#define SHOP_TEXT_SCALE 2
#define SHOP_LH         (8 * SHOP_TEXT_SCALE + 3)

void shop_draw(GBitmap *fb) {
    if (!s_open) return;

    GRect bounds = gbitmap_get_bounds(fb);
    int cx = bounds.size.w / 2;

    static char line[24];
    snprintf(line, sizeof(line), "MERCHANT - %dG", player_get_gold());
    bitfont_render_scaled(fb, line, cx, 4, JUSTIFY_CENTERV, SHOP_TEXT_SCALE);

    // Gossip line under title
    int mid = g_player.map_id;
    if (mid < 0) mid = 0;
    if (mid > 9) mid = 9;
    const char *gline = s_gossip[mid][s_gossip_page & 1];
    if (gline && gline[0]) {
        bitfont_render_scaled(fb, gline, cx, 4 + SHOP_LH,
                              JUSTIFY_CENTERV, SHOP_TEXT_SCALE);
    }

    draw_merchant_portrait(fb, 4, 64);

    int n = visible_count();
    if (n == 0) {
        bitfont_render_scaled(fb, "NOTHING LEFT", bounds.size.w - 4, 100,
                              JUSTIFY_RIGHT, SHOP_TEXT_SCALE);
        bitfont_render_scaled(fb, "BK:EXIT", cx, bounds.size.h - SHOP_LH - 4,
                              JUSTIFY_CENTERV, SHOP_TEXT_SCALE);
        return;
    }

    int list_top = 64;
    int icon_x = cx + 24;

    for (int vis = 0; vis < SHOP_VISIBLE; vis++) {
        int row = s_scroll + vis;
        if (row >= n) break;
        int si = stock_index_for_row(row);
        if (si < 0) break;
        const ShopItem *it = &s_stock[si];
        int row_y = list_top + vis * SHOP_ROW_H;
        bool selected = (row == s_selected);

        draw_icon(fb, it->icon_slot, icon_x, row_y);
        if (selected) {
            bitfont_render_scaled(fb, ">", icon_x - 16,
                                  row_y + SHOP_ICON_DRAW / 2 - 8,
                                  JUSTIFY_LEFT, SHOP_TEXT_SCALE);
            // Selected line: clarify upgrades
            if (it->kind == SHOP_WEAPON || it->kind == SHOP_ARMOR) {
                snprintf(line, sizeof(line), "%s UP %dG",
                         it->name, (int)it->price);
            } else {
                snprintf(line, sizeof(line), "%s %dG",
                         it->name, (int)it->price);
            }
            bitfont_render_scaled(fb, line, cx, list_top - SHOP_LH,
                                  JUSTIFY_CENTERV, SHOP_TEXT_SCALE);
        } else {
            static char price[12];
            snprintf(price, sizeof(price), "%d", (int)it->price);
            bitfont_render_scaled(fb, price, icon_x + SHOP_ICON_DRAW + 2,
                                  row_y + SHOP_ICON_DRAW / 2 - 8,
                                  JUSTIFY_LEFT, SHOP_TEXT_SCALE);
        }
    }

    if (s_status[0]) {
        bitfont_render_scaled(fb, s_status, cx,
                              bounds.size.h - SHOP_LH * 2 - 6,
                              JUSTIFY_CENTERV, SHOP_TEXT_SCALE);
    }
    bitfont_render_scaled(fb, "SL:BUY BK:EXIT", cx,
                          bounds.size.h - SHOP_LH - 4,
                          JUSTIFY_CENTERV, SHOP_TEXT_SCALE);
}
