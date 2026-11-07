/*
   +----------------------------------------------------------------------+
   | php-baremetal: touch-cst816s                                         |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

#include "cst816s_track.h"

bm_input_gesture_t cst_gesture_code(uint8_t code)
{
    switch (code) {
    case 0x01: return BM_INPUT_SWIPE_UP;
    case 0x02: return BM_INPUT_SWIPE_DOWN;
    case 0x03: return BM_INPUT_SWIPE_LEFT;
    case 0x04: return BM_INPUT_SWIPE_RIGHT;
    case 0x05: return BM_INPUT_TAP;
    case 0x0B: return BM_INPUT_DOUBLE_TAP;
    case 0x0C: return BM_INPUT_LONG_PRESS;
    default:   return (bm_input_gesture_t) 0;
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The direction of a swipe on the screen: the transpose of swap_xy turns up into left and down into right (and
 * back), then each mirror swaps its pair, in the order the coordinates get them. */
bm_input_gesture_t cst_track_orient(const cst_track_t *t, bm_input_gesture_t g)
{
    if (t->swap_xy) {
        switch (g) {
        case BM_INPUT_SWIPE_UP:    g = BM_INPUT_SWIPE_LEFT;  break;
        case BM_INPUT_SWIPE_LEFT:  g = BM_INPUT_SWIPE_UP;    break;
        case BM_INPUT_SWIPE_DOWN:  g = BM_INPUT_SWIPE_RIGHT; break;
        case BM_INPUT_SWIPE_RIGHT: g = BM_INPUT_SWIPE_DOWN;  break;
        default: break;
        }
    }
    if (t->mirror_x) {
        if (g == BM_INPUT_SWIPE_LEFT) {
            g = BM_INPUT_SWIPE_RIGHT;
        } else if (g == BM_INPUT_SWIPE_RIGHT) {
            g = BM_INPUT_SWIPE_LEFT;
        }
    }
    if (t->mirror_y) {
        if (g == BM_INPUT_SWIPE_UP) {
            g = BM_INPUT_SWIPE_DOWN;
        } else if (g == BM_INPUT_SWIPE_DOWN) {
            g = BM_INPUT_SWIPE_UP;
        }
    }
    return g;
}

bool cst_track_point(const cst_track_t *t, const uint8_t b[6], int *px, int *py)
{
    if ((b[1] & 0x0F) == 0) {
        return false;
    }
    int x = ((b[2] & 0x0F) << 8) | b[3];
    int y = ((b[4] & 0x0F) << 8) | b[5];
    if (t->swap_xy) {
        int s = x; x = y; y = s;
    }
    int maxx = t->w > 0 ? t->w - 1 : 0, maxy = t->h > 0 ? t->h - 1 : 0;
    x = clampi(x, 0, maxx);
    y = clampi(y, 0, maxy);
    *px = t->mirror_x ? maxx - x : x;
    *py = t->mirror_y ? maxy - y : y;
    return true;
}

int cst_track_report(cst_track_t *t, const uint8_t b[6], cst_event_t out[2])
{
    int n = 0;
    int x, y;

    if (cst_track_point(t, b, &x, &y)) {
        if (!t->down) {
            out[n++] = (cst_event_t) { BM_INPUT_PRESSED, x, y, (bm_input_gesture_t) 0 };
            t->gesture_seen = false;   /* a new press: a gesture of the same code is new again */
            t->late_left = 0;
        } else if (x != t->x || y != t->y) {
            out[n++] = (cst_event_t) { BM_INPUT_MOVED, x, y, (bm_input_gesture_t) 0 };
        }
        t->down = true;
        t->x = x;
        t->y = y;
    } else if (t->down) {
        /* The coordinates of a report without a finger are stale: the last point is the one to report. */
        out[n++] = (cst_event_t) { BM_INPUT_RELEASED, t->x, t->y, (bm_input_gesture_t) 0 };
        t->down = false;
        t->late_left = 2;
    }

    /* A code the chip repeats in the following reports is one gesture. */
    uint8_t code = b[0];
    bm_input_gesture_t g = cst_gesture_code(code);
    if (code != t->gesture && g != 0) {
        t->gesture_seen = true;
        out[n++] = (cst_event_t) { BM_INPUT_GESTURE, t->x, t->y, cst_track_orient(t, g) };
    }
    t->gesture = code;
    return n;
}

bool cst_track_late_wanted(cst_track_t *t)
{
    if (t->down || t->gesture_seen || t->late_left == 0) {
        return false;
    }
    t->late_left--;
    return true;
}

int cst_track_lost(cst_track_t *t, cst_event_t out[1])
{
    if (!t->down) {
        return 0;
    }
    out[0] = (cst_event_t) { BM_INPUT_RELEASED, t->x, t->y, (bm_input_gesture_t) 0 };
    t->down = false;
    return 1;
}
