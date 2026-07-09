#include "pebble.h"
#include "touch.h"
#include <math.h>

TouchZone touch_classify(int x, int y, int screen_w, int screen_h) {
    int cx = screen_w / 2;
    int cy = screen_h / 2;

    int dx = x - cx;
    int dy = y - cy;

    // distance from center (squared to avoid sqrt)
    int dist2 = dx*dx + dy*dy;

    // center tap — within 28px radius
    int center_r = 28;
    if (dist2 < center_r * center_r) return TOUCH_CENTER;

    // use angle to determine quadrant
    // atan2 returns -PI to PI, 0 = right, PI/2 = down
    // we remap: up = negative dy, down = positive dy
    float angle = atan2f((float)dy, (float)dx);
    // convert to degrees: 0=right, 90=down, 180/-180=left, -90=up
    float deg = angle * 57.2957795f;  // * (180/PI)

    // Movement zones: 4 quadrants
    // Top:    -135 to -45  (up)
    // Right:  -45  to  45  (right)
    // Bottom:  45  to 135  (down)
    // Left:   135 to 180 and -180 to -135 (left)

    if (deg >= -135.0f && deg < -45.0f) return TOUCH_UP;
    if (deg >=  -45.0f && deg <  45.0f) return TOUCH_RIGHT;
    if (deg >=   45.0f && deg < 135.0f) return TOUCH_DOWN;
    return TOUCH_LEFT;
}

// Simple three-strip layout — no icons needed
// Top 30% = forward, bottom 30% = back
// Left 20% = turn left, right 20% = turn right
// Center = menu

TouchZone touch_classify_movement(int x, int y, int w, int h) {
    // center tap — 30px radius
    int cx = w/2, cy = h/2;
    int dx = x-cx, dy = y-cy;
    if (dx*dx + dy*dy < 30*30) return TOUCH_CENTER;

    // horizontal strips take priority over sides
    if (y < h * 30/100) return TOUCH_UP;     // top 30% = forward
    if (y > h * 70/100) return TOUCH_DOWN;   // bottom 30% = back
    if (x < w * 20/100) return TOUCH_LEFT;   // left edge = turn left
    if (x > w * 80/100) return TOUCH_RIGHT;  // right edge = turn right
    return TOUCH_NONE;  // dead zone in middle — no accidental moves
}

// Combat zones use a different split:
// Top half = attack, bottom-left = item, bottom-right = run, bottom-center = confirm
TouchZone touch_classify_combat(int x, int y, int screen_w, int screen_h) {
    // int cx = screen_w / 2;
    int cy = screen_h / 2;
    // //int dx = x - cx;
    int dy = y - cy;

    // top half = attack
    if (dy < 0) return TOUCH_UP;  // UP = attack

    // bottom half — split into thirds by x
    int third = screen_w / 3;
    if (x < third)                     return TOUCH_LEFT;    // LEFT = item
    if (x > screen_w - third)          return TOUCH_RIGHT;   // RIGHT = run
    return TOUCH_CONFIRM;                                     // CENTER bottom = confirm
}