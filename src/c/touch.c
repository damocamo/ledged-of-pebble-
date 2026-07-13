#include "pebble.h"
#include "touch.h"

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

    // Integer octant replace for atan2f — same 90° quadrants as before,
    // without pulling the float library (~1KB of ROM near the RAM cap).
    // Top:    dy < 0 and |dy| >= |dx|
    // Bottom: dy > 0 and |dy| >= |dx|
    // Right:  dx > 0 and |dx| >  |dy|
    // Left:   otherwise
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (ady >= adx) {
        return (dy < 0) ? TOUCH_UP : TOUCH_DOWN;
    }
    return (dx < 0) ? TOUCH_LEFT : TOUCH_RIGHT;
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
    int cy = screen_h / 2;
    int dy = y - cy;

    // top half = attack
    if (dy < 0) return TOUCH_UP;  // UP = attack

    // bottom half — split into thirds by x
    int third = screen_w / 3;
    if (x < third)                     return TOUCH_LEFT;    // LEFT = item
    if (x > screen_w - third)          return TOUCH_RIGHT;   // RIGHT = run
    return TOUCH_CONFIRM;                                     // CENTER bottom = confirm
}
