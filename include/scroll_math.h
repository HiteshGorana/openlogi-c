/**
 * @file scroll_math.h
 * @brief Thumbwheel and scroll scaling math ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_SCROLL_MATH_H
#define OPENLOGI_SCROLL_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WHEEL_DIR_NONE = 0,
    WHEEL_DIR_POSITIVE = 1,  /* Up / Right */
    WHEEL_DIR_NEGATIVE = -1  /* Down / Left */
} wheel_dir_t;

typedef struct {
    int32_t native_res;    /* Native ratchets per revolution (e.g., 20) */
    int32_t diverted_res;  /* Diverted increments per revolution (e.g., 120) */
} wheel_resolution_t;

typedef struct {
    wheel_dir_t direction;
    uint32_t magnitude;
} wheel_rotation_t;

typedef struct {
    wheel_resolution_t resolution;
    double sensitivity;   /* 1.0 = Default */
    bool invert;
} scroll_scale_t;

/**
 * @brief Decompose signed delta into direction and positive magnitude.
 * Returns true if non-zero, false if 0.
 */
bool wheel_rotation_from_delta(int32_t delta, wheel_rotation_t *out_rot);

/**
 * @brief Initialize a scroll scale configuration.
 */
void scroll_scale_init(scroll_scale_t *scale, int32_t native_res, int32_t diverted_res, double sensitivity, bool invert);

/**
 * @brief Get fractional scroll amount per single raw increment.
 */
double scroll_scale_per_increment(const scroll_scale_t *scale);

/**
 * @brief Compute distance for a given number of raw ticks.
 */
double scroll_scale_compute_distance(const scroll_scale_t *scale, int32_t raw_increments);

/**
 * @brief Compute integer threshold required to trigger a discreet action.
 * Floored at 1 for high sensitivities.
 */
int32_t scroll_scale_action_threshold(double sensitivity);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SCROLL_MATH_H */
