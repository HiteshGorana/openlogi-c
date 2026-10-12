/**
 * @file gesture.c
 * @brief Implementation of gesture classification and swipe accumulator.
 */

#include "gesture.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>

static uint64_t get_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static inline int32_t sat_add(int32_t a, int32_t b)
{
    int64_t res = (int64_t)a + (int64_t)b;
    if (res > INT32_MAX) return INT32_MAX;
    if (res < INT32_MIN) return INT32_MIN;
    return (int32_t)res;
}

static inline int32_t sat_abs(int32_t a)
{
    if (a == INT32_MIN) return INT32_MAX;
    return a < 0 ? -a : a;
}

gesture_direction_t detect_swipe(int32_t dx, int32_t dy)
{
    int32_t abs_x = sat_abs(dx);
    int32_t abs_y = sat_abs(dy);
    int32_t dominant = abs_x > abs_y ? abs_x : abs_y;

    if (dominant < GESTURE_SWIPE_THRESHOLD) {
        return GESTURE_DIR_NONE;
    }

    int32_t scaled = (int32_t)(((int64_t)dominant * 35) / 100);
    int32_t cross_limit = GESTURE_SWIPE_DEADZONE > scaled ? GESTURE_SWIPE_DEADZONE : scaled;

    if (abs_x > abs_y) {
        if (abs_y > cross_limit) {
            return GESTURE_DIR_NONE; /* Too diagonal */
        }
        return dx > 0 ? GESTURE_DIR_RIGHT : GESTURE_DIR_LEFT;
    } else {
        if (abs_x > cross_limit) {
            return GESTURE_DIR_NONE; /* Too diagonal */
        }
        return dy > 0 ? GESTURE_DIR_DOWN : GESTURE_DIR_UP;
    }
}

void swipe_accumulator_init(swipe_accumulator_t *acc)
{
    if (!acc) return;
    memset(acc, 0, sizeof(*acc));
}

void swipe_accumulator_begin(swipe_accumulator_t *acc)
{
    if (!acc) return;
    acc->is_holding = true;
    acc->fired = false;
    acc->held_since_ms = get_now_ms();
    acc->accum_dx = 0;
    acc->accum_dy = 0;
}

bool swipe_accumulator_is_holding(const swipe_accumulator_t *acc)
{
    return acc ? acc->is_holding : false;
}

gesture_direction_t swipe_accumulator_accumulate(swipe_accumulator_t *acc, int32_t dx, int32_t dy)
{
    if (!acc || !acc->is_holding || acc->fired) {
        return GESTURE_DIR_NONE;
    }

    acc->accum_dx = sat_add(acc->accum_dx, dx);
    acc->accum_dy = sat_add(acc->accum_dy, dy);

    uint64_t now = get_now_ms();
    if (now >= acc->held_since_ms) {
        uint64_t elapsed = now - acc->held_since_ms;
        if (elapsed >= GESTURE_HOLD_FOR_SWIPE_MS) {
            gesture_direction_t dir = detect_swipe(acc->accum_dx, acc->accum_dy);
            if (dir != GESTURE_DIR_NONE) {
                acc->fired = true;
                return dir;
            }
        }
    }

    return GESTURE_DIR_NONE;
}

bool swipe_accumulator_end(swipe_accumulator_t *acc)
{
    if (!acc || !acc->is_holding) {
        return false;
    }

    bool was_click = !acc->fired;
    acc->is_holding = false;
    acc->fired = false;
    acc->accum_dx = 0;
    acc->accum_dy = 0;
    return was_click;
}

void swipe_accumulator_backdate_for_test(swipe_accumulator_t *acc, uint64_t ms)
{
    if (!acc || !acc->is_holding) return;
    if (acc->held_since_ms >= ms) {
        acc->held_since_ms -= ms;
    } else {
        acc->held_since_ms = 0;
    }
}

const char *gesture_direction_name(gesture_direction_t dir)
{
    switch (dir) {
        case GESTURE_DIR_UP:    return "Up";
        case GESTURE_DIR_DOWN:  return "Down";
        case GESTURE_DIR_LEFT:  return "Left";
        case GESTURE_DIR_RIGHT: return "Right";
        case GESTURE_DIR_CLICK: return "Click";
        case GESTURE_DIR_NONE:
        default:                return "None";
    }
}
