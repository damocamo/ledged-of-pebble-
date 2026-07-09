#pragma once
#include "pebble.h"

// Touch event types — mirrors button input names
typedef enum {
    TOUCH_NONE    = 0,
    TOUCH_UP      = 1,   // top zone — forward / scroll up / attack
    TOUCH_DOWN    = 2,   // bottom zone — backward / scroll down
    TOUCH_LEFT    = 3,   // left zone — turn left
    TOUCH_RIGHT   = 4,   // right zone — turn right / run
    TOUCH_CENTER  = 5,   // center tap — menu / confirm / item
    TOUCH_CONFIRM = 6,   // bottom-center combat zone
} TouchZone;

// Call this with raw touch coordinates
// Returns which zone was tapped
TouchZone touch_classify(int x, int y, int screen_w, int screen_h);
TouchZone touch_classify_movement(int x, int y, int w, int h);

// Called from main.c touch handler
void touch_handle(int x, int y);