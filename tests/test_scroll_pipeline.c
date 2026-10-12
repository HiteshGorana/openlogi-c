/**
 * @file test_scroll_pipeline.c
 * @brief Regression tests for scroll preferences, direct scaling, and animation phase pipeline.
 */

#include "scroll_pipeline.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

static void test_scroll_prefs_atomic_pack_unpack(void)
{
    /* 1. Smooth scroll enabled, sensitivity 14 */
    uint8_t byte = scroll_prefs_encode(true, 14);
    assert(byte & SCROLL_SMOOTH_FLAG);
    assert((byte & SCROLL_SENSITIVITY_MASK) == 14);

    bool smooth = false;
    uint8_t sens = 0;
    scroll_prefs_decode(byte, &smooth, &sens);
    assert(smooth == true);
    assert(sens == 14);

    /* 2. Smooth scroll disabled, sensitivity 80 */
    byte = scroll_prefs_encode(false, 80);
    assert(!(byte & SCROLL_SMOOTH_FLAG));
    assert((byte & SCROLL_SENSITIVITY_MASK) == 80);

    scroll_prefs_decode(byte, &smooth, &sens);
    assert(smooth == false);
    assert(sens == 80);

    /* 3. Out-of-bounds sensitivity clamping (0 clamped to 1, 150 clamped to 100) */
    byte = scroll_prefs_encode(true, 0);
    scroll_prefs_decode(byte, &smooth, &sens);
    assert(sens == 1);

    byte = scroll_prefs_encode(false, 150);
    scroll_prefs_decode(byte, &smooth, &sens);
    assert(sens == 100);
}

static void test_scroll_direct_scaled_output(void)
{
    /* Sensitivity 14 is 1.0x baseline */
    double delta_base = scroll_scale_direct(2.0, 14);
    assert(fabs(delta_base - 2.0) < 1e-6);

    /* Sensitivity 7 is 0.5x */
    double delta_half = scroll_scale_direct(2.0, 7);
    assert(fabs(delta_half - 1.0) < 1e-6);

    /* Sensitivity 28 is 2.0x */
    double delta_double = scroll_scale_direct(2.0, 28);
    assert(fabs(delta_double - 4.0) < 1e-6);
}

static void test_scroll_phase_lifecycle(void)
{
    scroll_frame_t f1 = { .x = 0.0, .y = 1.0, .phase = SCROLL_PHASE_BEGAN };
    assert(f1.phase == SCROLL_PHASE_BEGAN);

    scroll_frame_t f2 = { .x = 0.0, .y = 2.5, .phase = SCROLL_PHASE_CHANGED };
    assert(f2.phase == SCROLL_PHASE_CHANGED);

    scroll_frame_t f3 = { .x = 0.0, .y = 0.0, .phase = SCROLL_PHASE_ENDED };
    assert(f3.phase == SCROLL_PHASE_ENDED);

    scroll_frame_t f_cancel = { .x = 0.0, .y = 0.0, .phase = SCROLL_PHASE_CANCELLED };
    assert(f_cancel.phase == SCROLL_PHASE_CANCELLED);
}

static void test_scroll_momentum_decay_rate(void)
{
    double v = 10.0;
    double friction = 0.90;

    int steps = 0;
    while (v > 0.0 && steps < 100) {
        v = scroll_momentum_step(v, friction);
        steps++;
    }

    /* Velocity must decay smoothly to 0 in finite steps */
    assert(v == 0.0);
    assert(steps > 10 && steps < 60);
}

static void test_scroll_horizontal_discrete_ticks(void)
{
    /* 120 fine increments per revolution, 20 ratchet notches -> 6 fine ticks per notch */
    assert(scroll_discrete_notches(0, 6) == 0);
    assert(scroll_discrete_notches(5, 6) == 0);   /* Sub-notch dropped */
    assert(scroll_discrete_notches(6, 6) == 1);   /* Exactly 1 notch */
    assert(scroll_discrete_notches(18, 6) == 3);  /* Exactly 3 notches */
    assert(scroll_discrete_notches(-12, 6) == -2); /* Negative direction */
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_scroll_pipeline", "Scroll Preferences, Scaling & Momentum Pipeline");

    TEST_CASE_RUN(g_suite, test_scroll_prefs_atomic_pack_unpack);
    TEST_CASE_RUN(g_suite, test_scroll_direct_scaled_output);
    TEST_CASE_RUN(g_suite, test_scroll_phase_lifecycle);
    TEST_CASE_RUN(g_suite, test_scroll_momentum_decay_rate);
    TEST_CASE_RUN(g_suite, test_scroll_horizontal_discrete_ticks);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
