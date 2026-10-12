/**
 * @file scroll_pipeline.h
 * @brief Scroll preferences encoding, scaling, and animation phase pipeline ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_SCROLL_PIPELINE_H
#define OPENLOGI_SCROLL_PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCROLL_SMOOTH_FLAG     0x80
#define SCROLL_SENSITIVITY_MASK 0x7F
#define SCROLL_DEFAULT_SENSITIVITY 14

typedef enum {
    SCROLL_PHASE_NONE = 0,
    SCROLL_PHASE_BEGAN,
    SCROLL_PHASE_CHANGED,
    SCROLL_PHASE_ENDED,
    SCROLL_PHASE_CANCELLED
} scroll_phase_t;

typedef struct {
    double x;
    double y;
    scroll_phase_t phase;
} scroll_frame_t;

/**
 * @brief Encode smooth scroll flag and 1..100 sensitivity into a single byte.
 */
uint8_t scroll_prefs_encode(bool smooth_scroll, uint8_t sensitivity);

/**
 * @brief Decode single-byte preferences snapshot.
 */
void scroll_prefs_decode(uint8_t encoded, bool *out_smooth, uint8_t *out_sensitivity);

/**
 * @brief Scale direct wheel ticks according to sensitivity setting.
 */
double scroll_scale_direct(double raw_ticks, uint8_t sensitivity);

/**
 * @brief Compute momentum decay step for smooth inertial scrolling.
 */
double scroll_momentum_step(double velocity, double friction);

/**
 * @brief Convert continuous high-resolution increments to discrete ratchet notches.
 */
int32_t scroll_discrete_notches(int32_t fine_ticks, int32_t ticks_per_notch);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SCROLL_PIPELINE_H */
