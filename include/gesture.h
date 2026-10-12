/**
 * @file gesture.h
 * @brief Gesture classification, swipe accumulator, and direction state machine.
 */

#ifndef OPENLOGI_GESTURE_H
#define OPENLOGI_GESTURE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum dominant travel (units) before a held gesture commits to a direction */
#define GESTURE_SWIPE_THRESHOLD 50

/** Maximum cross-axis travel allowed at threshold */
#define GESTURE_SWIPE_DEADZONE  40

/** Minimum hold duration (ms) before travel can commit to a swipe */
#define GESTURE_HOLD_FOR_SWIPE_MS 160

typedef enum {
    GESTURE_DIR_NONE = 0,
    GESTURE_DIR_UP,     /* Swipe up (negative dy) */
    GESTURE_DIR_DOWN,   /* Swipe down (positive dy) */
    GESTURE_DIR_LEFT,   /* Swipe left (negative dx) */
    GESTURE_DIR_RIGHT,  /* Swipe right (positive dx) */
    GESTURE_DIR_CLICK   /* Press-and-release without swiping (plain click) */
} gesture_direction_t;

/**
 * @brief Mid-swipe state machine tracking travel deltas and commit state.
 */
typedef struct {
    bool is_holding;
    bool fired;
    uint64_t held_since_ms;
    int32_t accum_dx;
    int32_t accum_dy;
} swipe_accumulator_t;

/**
 * @brief Callback invoked when a gesture commits to a direction or click.
 */
typedef void (*gesture_commit_callback_t)(gesture_direction_t direction, void *user_data);

/**
 * @brief Classify raw-XY travel into a committed swipe direction.
 */
gesture_direction_t detect_swipe(int32_t dx, int32_t dy);

/**
 * @brief Initialize swipe accumulator.
 */
void swipe_accumulator_init(swipe_accumulator_t *acc);

/**
 * @brief Begin a fresh hold (button press down).
 */
void swipe_accumulator_begin(swipe_accumulator_t *acc);

/**
 * @brief Check if button is currently in a held state.
 */
bool swipe_accumulator_is_holding(const swipe_accumulator_t *acc);

/**
 * @brief Feed relative travel deltas into the accumulator.
 * Returns committed direction exactly once per hold, or GESTURE_DIR_NONE.
 */
gesture_direction_t swipe_accumulator_accumulate(swipe_accumulator_t *acc, int32_t dx, int32_t dy);

/**
 * @brief End current hold (button release).
 * @return true if the hold ended as a plain click without committing a swipe.
 */
bool swipe_accumulator_end(swipe_accumulator_t *acc);

/**
 * @brief Test helper to simulate passage of time for hold threshold.
 */
void swipe_accumulator_backdate_for_test(swipe_accumulator_t *acc, uint64_t ms);

/**
 * @brief Human-readable name for a gesture direction.
 */
const char *gesture_direction_name(gesture_direction_t dir);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_GESTURE_H */
