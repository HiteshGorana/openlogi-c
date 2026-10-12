/**
 * @file scroll_math.c
 * @brief Implementation of thumbwheel and scroll math ported from openlogi-agent-core.
 */

#include "scroll_math.h"

#include <math.h>
#include <stdlib.h>

#define DEFAULT_ACTION_THRESHOLD 14

bool wheel_rotation_from_delta(int32_t delta, wheel_rotation_t *out_rot)
{
    if (delta == 0) {
        if (out_rot) {
            out_rot->direction = WHEEL_DIR_NONE;
            out_rot->magnitude = 0;
        }
        return false;
    }

    if (out_rot) {
        out_rot->direction = (delta > 0) ? WHEEL_DIR_POSITIVE : WHEEL_DIR_NEGATIVE;
        out_rot->magnitude = (uint32_t)labs(delta);
    }
    return true;
}

void scroll_scale_init(scroll_scale_t *scale, int32_t native_res, int32_t diverted_res, double sensitivity, bool invert)
{
    if (!scale) return;
    scale->resolution.native_res = (native_res > 0) ? native_res : 1;
    scale->resolution.diverted_res = (diverted_res > 0) ? diverted_res : 1;
    scale->sensitivity = (sensitivity > 0.0) ? sensitivity : 1.0;
    scale->invert = invert;
}

double scroll_scale_per_increment(const scroll_scale_t *scale)
{
    if (!scale) return 1.0;
    double ratio = 1.0;
    if (scale->resolution.diverted_res > 0 && scale->resolution.native_res > 0) {
        ratio = (double)scale->resolution.native_res / (double)scale->resolution.diverted_res;
    }
    return ratio * scale->sensitivity;
}

double scroll_scale_compute_distance(const scroll_scale_t *scale, int32_t raw_increments)
{
    if (!scale) return (double)raw_increments;
    double per_inc = scroll_scale_per_increment(scale);
    double dist = (double)raw_increments * per_inc;
    return scale->invert ? -dist : dist;
}

int32_t scroll_scale_action_threshold(double sensitivity)
{
    if (sensitivity <= 0.0) sensitivity = 1.0;
    int32_t thresh = (int32_t)round((double)DEFAULT_ACTION_THRESHOLD / sensitivity);
    return (thresh < 1) ? 1 : thresh;
}
