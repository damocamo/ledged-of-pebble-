#include "pebble.h"
#include "dialog.h"
#include "bitfont.h"
#include "event.h" 

// ============================================================
// dialog.c
// Multi-page dialog system for storytelling and NPC interactions
// ============================================================
//
// DIALOG STRUCTURE:
// Dialogs consist of:
// - Title: Displayed at top (e.g., "MAREN", "THE KEEPER", "OLD BONES")
// - Pages: Array of text pages, each up to ~56 characters (4 lines × 14 chars)
// - Page count: Total number of pages in the dialog
//
// DIALOG FLOW:
// 1. Event system calls dialog_open() with a DialogDef
// 2. Dialog takes over input and rendering
// 3. Player navigates through pages with UP/DOWN buttons
// 4. On last page, DOWN closes dialog and resumes event execution
// 5. BACK button can exit early at any time
//
// TEXT FORMATTING:
// - Text is rendered using custom bitfont (pixelated 8×8 characters)
// - Newlines (\n) are supported for line breaks
// - Maximum 14 characters per line for readability on small screen
// - Text is centered both horizontally and vertically
// - Example: "YOU'RE\nSTARTING TO\nREMEMBER...\nAREN'T YOU?"
//
// DIALOG TYPES:
// 1. Environmental storytelling (skulls, inscriptions, journals)
//    - Title: "OLD BONES", "JOURNAL FRAGMENT"
//    - Usually 2-5 pages of atmospheric text
//
// 2. NPC conversations (Maren, Warden)
//    - Title: Character name
//    - Longer dialogs (7-10+ pages) revealing plot
//    - State-based progression via flags
//
// 3. System messages (victory, defeat, end demo)
//    - Title: "LEGEND OF PEBBLE"
//    - Brief informational text
//
// EVENT INTEGRATION:
// Dialog pauses event execution via CMD_DIALOG:
// { .type = CMD_DIALOG, .dialog = { &my_dialog } }
//
// When dialog closes (via exit or completing all pages):
// - Calls event_resume() to continue event command chain
// - Subsequent commands after CMD_DIALOG now execute
// - This allows: Dialog → Give Item → Change Map, etc.
//
// NAVIGATION:
// - UP button: Previous page (if not on first page)
// - DOWN button: Next page, or exit on last page
// - BACK button: Exit dialog immediately (skips remaining pages)
// - Page indicator shows current position (e.g., "3/7")
//
// STATE MANAGEMENT:
// Dialog system is stateless between openings:
// - No memory of previously viewed dialogs
// - State tracking done via flags in event system
// - Same dialog can be shown multiple times
//
// Example multi-stage NPC dialog:
// Event checks flags to determine which dialog stage to show:
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_FREED_MAREN, 58 } },
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_SUBJECT6_MAREN, 57 } },
// { .type = CMD_FLAG_CHECK, .flag_check = { FLAG_JOURNAL_MAREN, 56 } },
// { .type = CMD_JUMP, .jump = { 55 } },  // Default to intro dialog
//
// VISUAL LAYOUT:
// Screen divided into three sections:
// - Top: Title (cy - 114 + 27)
// - Middle: Dialog text (cy, centered)
// - Bottom: Page counter (left) and navigation hint (center)
//
// RENDERING:
// - Only renders when dialog is open (s_dialog != NULL)
// - Overlays on top of game world
// - Uses layer_mark_dirty() to trigger redraws
// - World remains visible but input is blocked
//
// PERFORMANCE:
// - Lightweight: Only stores pointer and page index
// - No dynamic memory allocation
// - Dialog text stored in const arrays in map event files
//
// ============================================================

static const DialogDef *s_dialog     = NULL;
static int              s_page       = 0;
static Layer           *s_canvas_ref = NULL;

void dialog_init(Layer *canvas) {
    s_canvas_ref = canvas;
}

void dialog_open(const DialogDef *def) {
    s_dialog = def;
    s_page   = 0;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

bool dialog_is_open(void) {
    return s_dialog != NULL;
}

void dialog_input_next(void) {
    if (!s_dialog) return;
    if (s_page < s_dialog->page_count - 1) {
        s_page++;
    } else {
        s_dialog = NULL;
        event_resume();  // ← ADD THIS — continue event chain
    }
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void dialog_input_prev(void) {
    if (!s_dialog) return;
    if (s_page > 0) s_page--;
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void dialog_input_exit(void) {
    s_dialog = NULL;
    event_resume();
    if (s_canvas_ref) layer_mark_dirty(s_canvas_ref);
}

void dialog_draw(GBitmap *fb) {
    if (!s_dialog) return;

    GRect fb_bounds = gbitmap_get_bounds(fb);
    int cx = fb_bounds.size.w / 2;
    int cy = fb_bounds.size.h / 2;

    // Title
    bitfont_render(fb, s_dialog->title, cx, cy - 114 + 27, JUSTIFY_CENTER);

    // Current page — centered in the middle of the screen
    // bitfont_render handles \n and vertical centering automatically
    const DialogPage *page = &s_dialog->pages[s_page];
    if (page->text) {
        bitfont_render(fb, page->text, cx, cy, JUSTIFY_CENTER);
    }

    // Page indicator
    static char page_buf[24];
    snprintf(page_buf, sizeof(page_buf), "%d/%d",
             s_page + 1, s_dialog->page_count);
    bitfont_render(fb, page_buf, cx-100+3, cy + 114 - 27*2, JUSTIFY_LEFT);

    // Navigation hint
    bitfont_render(fb,
        s_page < s_dialog->page_count - 1 ? "DN:NEXT" : "DN:EXIT",
        cx, cy + 114 - 15, JUSTIFY_CENTER);
}