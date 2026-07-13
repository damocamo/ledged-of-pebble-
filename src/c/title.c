#include "pebble.h"
#include "title.h"
#include "bitfont.h"
#include "player.h"
#include "map.h"
#include "event.h"
#include "encounter.h"
#include "save.h"
#include "renderer.h"

#define TITLE_VIEW_X       3
#define TITLE_VIEW_Y       2
#define TITLE_VIEW_FACING  FACING_NORTH

// ---- State --------------------------------------------------------------
typedef enum {
    TITLE_ITEM_CONTINUE  = 0,
    TITLE_ITEM_NEW_GAME  = 1,
} TitleItem;

static bool   s_active      = true;
static int    s_selected    = 0;
static bool   s_has_save    = false;
static Layer *s_canvas_ref  = NULL;

// menu items — dynamic based on save existence
#define MAX_MENU_ITEMS 2
static const char *s_menu_items[MAX_MENU_ITEMS];
static int         s_menu_count = 0;

static void build_menu(void) {
    s_menu_count = 0;
    if (s_has_save) {
        s_menu_items[s_menu_count++] = "CONTINUE";
    }
    s_menu_items[s_menu_count++] = "NEW GAME";
    // clamp selection
    if (s_selected >= s_menu_count) s_selected = 0;
}

// ---- Public API ---------------------------------------------------------
void title_init(Layer *canvas, GBitmap *fb_ref) {
    s_canvas_ref = canvas;
    s_active     = true;
    s_selected   = 0;
    // check if a save exists
    s_has_save   = save_exists();
    build_menu();
}

bool title_is_active(void) {
    return s_active;
}

void title_input_up(void) {
    if (!s_active) return;
    if (s_selected > 0) s_selected--;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void title_input_down(void) {
    if (!s_active) return;
    if (s_selected < s_menu_count - 1) s_selected++;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void title_input_select(void) {
    if (!s_active) return;

#ifdef SCREENSHOT_START_MAP
    // Debug harness: warp straight into a given campaign level so the emulator
    // capture script can screenshot every floor (and the victory screen)
    // without playing through the puzzles. Mirrors microdeck's start-at-floor.
    {
        static const struct { int8_t x, y; uint8_t facing; } k_warp[] = {
            { MAP1_START_X,  MAP1_START_Y,  MAP1_START_FACING  },
            { MAP2_START_X,  MAP2_START_Y,  MAP2_START_FACING  },
            { MAP3_START_X,  MAP3_START_Y,  MAP3_START_FACING  },
            { MAP4_START_X,  MAP4_START_Y,  MAP4_START_FACING  },
            { MAP5_START_X,  MAP5_START_Y,  MAP5_START_FACING  },
            { MAP6_START_X,  MAP6_START_Y,  MAP6_START_FACING  },
            { MAP7_START_X,  MAP7_START_Y,  MAP7_START_FACING  },
            { MAP8_START_X,  MAP8_START_Y,  MAP8_START_FACING  },
            { MAP9_START_X,  MAP9_START_Y,  MAP9_START_FACING  },
            { MAP10_START_X, MAP10_START_Y, MAP10_START_FACING },
        };
        int m = SCREENSHOT_START_MAP;
        if (m < 0) m = 0;
        if (m > 9) m = 9;
        save_delete();
        player_init();
        g_player.map_id = (uint8_t)m;
        g_player.x      = k_warp[m].x;
        g_player.y      = k_warp[m].y;
        g_player.facing = k_warp[m].facing;
        map_init_for((uint8_t)m);
        event_load_map(m);
        encounter_init(m);
#ifdef SCREENSHOT_END
        event_run(1);          // fire map 10's VICTORY dialog directly
#else
        start_event_run();     // show the "LEVEL N" banner
#endif
        s_active = false;
        if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
        return;
    }
#endif

    const char *item = s_menu_items[s_selected];

    if (strcmp(item, "NEW GAME") == 0) {
        // wipe any existing save and start fresh
        save_delete();
        flags_clear();
        map_init_for(0);
        event_load_map(0);
        encounter_init(0);
        player_init();
        start_event_run();  // intro dialog
        s_active = false;

    } else if (strcmp(item, "CONTINUE") == 0) {
        map_init_for(0);
        event_load_map(0);
        encounter_init(0);
        player_init();
        if (!save_read()) {
            // Corrupt / missing save — fall back to a fresh start + intro.
            save_delete();
            start_event_run();
        }
        // Successful Continue: skip start_event_run so intros / LEVEL toasts
        // do not replay every resume.
        s_active = false;
    }

    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

// ---- Draw ---------------------------------------------------------------
void title_draw(GBitmap *fb, GContext *ctx) {
    
    if (!s_active) return;

    for (int slot = 0; slot < VIEW_SLOT_COUNT; slot++) {
        Offset off = VIEW_OFFSETS[slot][TITLE_VIEW_FACING];
        int map_x  = TITLE_VIEW_X + off.dx;
        int map_y  = TITLE_VIEW_Y + off.dy;
        draw_slot (fb, ctx, slot, map_x, map_y);
        draw_decor(fb, ctx, slot, map_x, map_y);
    }

    GRect fb_bounds = gbitmap_get_bounds(fb);
    int cx = fb_bounds.size.w / 2;
    int cy = fb_bounds.size.h / 2;
    // Leave margin so glyphs never kiss the bezel / round corners.
    int max_w = fb_bounds.size.w - 16;

    // Largest scale (3..1) whose widest line still fits on screen.
    static const char *title_txt = "LEGEND OF\nPEBBLE";
    static const char *tag_txt   = "A WATCH\nAWAY FROM HOME";

    int title_scale = 3;
    while (title_scale > 1 && bitfont_calc_width_scaled("LEGEND OF", title_scale) > max_w)
        title_scale--;

    int tag_scale = 2;
    while (tag_scale > 1 && bitfont_calc_width_scaled("AWAY FROM HOME", tag_scale) > max_w)
        tag_scale--;

    // Menu: selected item is widest ("[ NEW GAME ]" / "[ CONTINUE ]")
    int menu_scale = 3;
    {
        int widest = 0;
        for (int i = 0; i < s_menu_count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "[ %s ]", s_menu_items[i]);
            int w = bitfont_calc_width_scaled(buf, 1);  // unscaled base
            if (w > widest) widest = w;
        }
        while (menu_scale > 1 && widest * menu_scale > max_w)
            menu_scale--;
    }

    int title_h = bitfont_calc_height_scaled(title_txt, title_scale);
    int tag_h   = bitfont_calc_height_scaled(tag_txt,   tag_scale);

    // Title near the top, fully on-screen
    int title_y = 4 + title_h / 2;
    bitfont_render_scaled(fb, title_txt, cx, title_y, JUSTIFY_CENTER, title_scale);

    // Menu items
    int menu_line_h = 8 * menu_scale + 6;
    int menu_top = cy - ((s_menu_count - 1) * menu_line_h) / 2;
    for (int i = 0; i < s_menu_count; i++) {
        int item_y = menu_top + i * menu_line_h;
        if (i == s_selected) {
            static char sel_buf[32];
            snprintf(sel_buf, sizeof(sel_buf), "[ %s ]", s_menu_items[i]);
            bitfont_render_scaled(fb, sel_buf, cx, item_y, JUSTIFY_CENTERV, menu_scale);
        } else {
            bitfont_render_scaled(fb, s_menu_items[i], cx, item_y, JUSTIFY_CENTERV, menu_scale);
        }
    }

    // Tagline near the bottom, fully on-screen
    int tag_y = fb_bounds.size.h - 4 - tag_h / 2;
    bitfont_render_scaled(fb, tag_txt, cx, tag_y, JUSTIFY_CENTER, tag_scale);
}