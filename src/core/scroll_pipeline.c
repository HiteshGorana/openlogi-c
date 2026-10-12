/**
 * @file scroll_pipeline.c
 * @brief Implementation of scroll preferences encoding, scaling, and animation phase pipeline.
 */

#include "scroll_pipeline.h"

#include <math.h>

uint8_t scroll_prefs_encode(bool smooth_scroll, uint8_t sensitivity)
{
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 100) sensitivity = 100;

    uint8_t byte = sensitivity & SCROLL_SENSITIVITY_MASK;
    if (smooth_scroll) {
        byte |= SCROLL_SMOOTH_FLAG;
    }
    return byte;
}

void scroll_prefs_decode(uint8_t encoded, bool *out_smooth, uint8_t *out_sensitivity)
{
    if (out_smooth) {
        *out_smooth = (encoded & SCROLL_SMOOTH_FLAG) != 0;
    }
    if (out_sensitivity) {
        uint8_t s = encoded & SCROLL_SENSITIVITY_MASK;
        if (s < 1) s = 1;
        if (s > 100) s = 100;
        *out_sensitivity = s;
    }
}

double scroll_scale_direct(double raw_ticks, uint8_t sensitivity)
{
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 100) sensitivity = 100;

    double factor = (double)sensitivity / (double)SCROLL_DEFAULT_SENSITIVITY;
    return raw_ticks * factor;
}

double scroll_momentum_step(double velocity, double friction)
{
    if (friction <= 0.0 || friction >= 1.0) friction = 0.92;
    double next = velocity * friction;
    if (fabs(next) < 0.05) {
        return 0.0; /* Stopped */
    }
    return next;
}

int32_t scroll_discrete_notches(int32_t fine_ticks, int32_t ticks_per_notch)
{
    if (ticks_per_notch <= 0) ticks_per_notch = 1;
    return fine_ticks / ticks_per_notch;
}
