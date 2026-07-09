#pragma once
#include "pebble.h"
#include "touch.h"

// ---- Item definition ----------------------------------------------------
typedef enum {
    ITEM_TYPE_CONSUMABLE = 0,  // use and deplete quantity
    ITEM_TYPE_KEY        = 1,  // use triggers a map event
} ItemType;

typedef struct {
    const char *name;
    ItemType    type;
    uint8_t     icon_slot;
    int8_t      use_event;  // event to fire when used, -1 = no event
    int8_t      use_event2;  // event to fire when used, -1 = no event
} ItemDef;

// ---- Menu API -----------------------------------------------------------
void menu_init(Layer *canvas, GBitmap *icon_atlas);
bool menu_is_open(void);
void menu_open(void);
void menu_close(void);

void menu_input_up(void);
void menu_input_down(void);
void menu_input_select(void);
void menu_input_back(void);

void menu_give_item(int item_index, int quantity);

void menu_draw(GBitmap *fb);
void menu_tick(void);   // call from an AppTimer for smooth scroll

int menu_get_selected(void);   // returns s_selected
int menu_get_scroll(void);     // returns s_scroll_y
void menu_touch_select_row(int vis_index); // jump to visible row

int  menu_row_zone_count(void);
int  menu_row_zone_y(int i);
int  menu_row_zone_h(void);
int  menu_get_selected(void);
int  menu_get_scroll(void);
void menu_touch_select_row(int vis_index);