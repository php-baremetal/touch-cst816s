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

/* Pure tracking logic of the CST816S: 6-byte reports to touch events. No PHP, no I2C, so it runs on the host. */
#ifndef CST816S_TRACK_H
#define CST816S_TRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "bm_input.h"

typedef struct {
    int w, h;                          /* screen size, after swap_xy */
    bool mirror_x, mirror_y, swap_xy;  /* orientation, applied as the display does: swap, then mirror */
    bool down;                         /* a finger is down */
    int x, y;                          /* the last point, in screen coordinates */
    uint8_t gesture;                   /* the last gesture code of the chip (raw), to report a code once */
    bool gesture_seen;                 /* a Gesture was raised since the last press */
    uint8_t late_left;                 /* extra service runs still wanted after a release (late tap codes) */
} cst_track_t;

typedef struct { bm_input_kind_t kind; int x, y; bm_input_gesture_t gesture; } cst_event_t;

/* From one 6-byte report (gesture, fingers, Xh, Xl, Yh, Yl): updates t and fills up to 2 events (the finger
 * event first, then a Gesture), returns how many. Coordinates are clamped to the screen and oriented; a swipe
 * direction gets the same swap and mirror as the coordinates. */
int cst_track_report(cst_track_t *t, const uint8_t b[6], cst_event_t out[2]);

/* With double tap on, the chip puts the tap / double tap code into a report ~260 ms AFTER the release. True when
 * the service should ask for one more run (about 350 ms later): no finger down, no gesture since the last press,
 * and fewer than 2 such runs since the release (a bounded budget, so an idle chip is not polled forever). */
bool cst_track_late_wanted(cst_track_t *t);

/* The oriented screen point of a report with a finger down: false (x and y untouched) when it has none. */
bool cst_track_point(const cst_track_t *t, const uint8_t b[6], int *x, int *y);

/* A swipe direction (or any gesture) under the orientation of t; non-swipes come back unchanged. */
bm_input_gesture_t cst_track_orient(const cst_track_t *t, bm_input_gesture_t g);

/* The finger is gone without a report saying so (a failed read while down): Released with the last point.
 * Returns 1 and fills out[0], or 0 when no finger was down. */
int cst_track_lost(cst_track_t *t, cst_event_t out[1]);

/* 0x01..0x05, 0x0B, 0x0C to the enum (swipe up, down, left, right, tap, double tap, long press); 0 otherwise. */
bm_input_gesture_t cst_gesture_code(uint8_t code);

#endif
