/**
 * @file test_wheel_math.c
 * @brief Regression tests for thumbwheel and scroll math ported from openlogi-agent-core.
 */

#include "scroll_math.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#define EPSILON 1e-9

static void test_wheel_rotation_direction_and_magnitude(void)
{
    wheel_rotation_t rot;

    /* Positive delta */
    assert(wheel_rotation_from_delta(3, &rot));
    assert(rot.direction == WHEEL_DIR_POSITIVE);
    assert(rot.magnitude == 3);

    /* Negative delta */
    assert(wheel_rotation_from_delta(-5, &rot));
    assert(rot.direction == WHEEL_DIR_NEGATIVE);
    assert(rot.magnitude == 5);

    /* Zero delta */
    assert(!wheel_rotation_from_delta(0, &rot));
    assert(rot.direction == WHEEL_DIR_NONE);
    assert(rot.magnitude == 0);
}

static void test_wheel_multiplier_unscaled(void)
{
    scroll_scale_t scale;

    /* Default sensitivity (1.0) on 1:1 unscaled wheel */
    scroll_scale_init(&scale, 1, 1, 1.0, false);
    assert(fabs(scroll_scale_per_increment(&scale) - 1.0) < EPSILON);

    /* High sensitivity */
    scroll_scale_init(&scale, 1, 1, 2.5, false);
    assert(scroll_scale_per_increment(&scale) > 1.9);

    /* Low sensitivity */
    scroll_scale_init(&scale, 1, 1, 0.05, false);
    assert(scroll_scale_per_increment(&scale) < 0.1);
}

static void test_wheel_action_threshold_scaling(void)
{
    /* Default threshold at sensitivity 1.0 */
    int32_t t_default = scroll_scale_action_threshold(1.0);
    assert(t_default == 14);

    /* Low sensitivity requires more increments */
    int32_t t_low = scroll_scale_action_threshold(0.5);
    assert(t_low == 28);
    assert(t_low > t_default);

    /* High sensitivity floors at 1 */
    int32_t t_high = scroll_scale_action_threshold(30.0);
    assert(t_high == 1);
}

static void test_wheel_diverted_revolution_scaling(void)
{
    /* MX Master over Bolt: 20 native ratchets / revolution, 120 diverted increments / revolution */
    scroll_scale_t scale;
    scroll_scale_init(&scale, 20, 120, 1.0, false);

    /* One full revolution: 120 increments */
    double distance = 0.0;
    for (int i = 0; i < 120; i++) {
        distance += scroll_scale_compute_distance(&scale, 1);
    }

    /* Total distance MUST equal exactly 20.0 native scroll units */
    assert(fabs(distance - 20.0) < EPSILON);

    /* At 2x sensitivity, distance doubles to 40.0 */
    scroll_scale_init(&scale, 20, 120, 2.0, false);
    double dist_2x = scroll_scale_compute_distance(&scale, 120);
    assert(fabs(dist_2x - 40.0) < EPSILON);
}

static void test_wheel_inversion(void)
{
    scroll_scale_t scale;
    scroll_scale_init(&scale, 20, 120, 1.0, true /* invert */);

    double dist = scroll_scale_compute_distance(&scale, 120);
    assert(fabs(dist - (-20.0)) < EPSILON);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_wheel_math", "Thumbwheel & Mathematical Scroll Scaling");

    TEST_CASE_RUN(g_suite, test_wheel_rotation_direction_and_magnitude);
    TEST_CASE_RUN(g_suite, test_wheel_multiplier_unscaled);
    TEST_CASE_RUN(g_suite, test_wheel_action_threshold_scaling);
    TEST_CASE_RUN(g_suite, test_wheel_diverted_revolution_scaling);
    TEST_CASE_RUN(g_suite, test_wheel_inversion);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
