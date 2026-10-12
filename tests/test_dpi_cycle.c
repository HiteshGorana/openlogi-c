/**
 * @file test_dpi_cycle.c
 * @brief Regression tests for DPI presets cycle and boundary clamping ported from openlogi-agent-core.
 */

#include "dpi_cycle.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void test_dpi_cycle_advancement_and_wrap(void)
{
    uint16_t presets[4] = { 800, 1200, 1600, 2400 };
    dpi_cycle_state_t cycle;

    assert(dpi_cycle_init(&cycle, presets, 4));
    assert(dpi_cycle_current(&cycle) == 800);

    /* Advance: 800 -> 1200 -> 1600 -> 2400 */
    assert(dpi_cycle_next(&cycle) == 1200);
    assert(dpi_cycle_next(&cycle) == 1600);
    assert(dpi_cycle_next(&cycle) == 2400);

    /* Overflow: wraps around to index 0 (800) */
    assert(dpi_cycle_next(&cycle) == 800);
    assert(dpi_cycle_current(&cycle) == 800);
}

static void test_dpi_cycle_offline_disconnect_and_restore(void)
{
    uint16_t presets[4] = { 800, 1200, 1600, 2400 };
    dpi_cycle_state_t cycle;

    assert(dpi_cycle_init(&cycle, presets, 4));
    assert(dpi_cycle_next(&cycle) == 1200);
    assert(dpi_cycle_next(&cycle) == 1600);

    /* Device disconnects / sleeps */
    dpi_cycle_set_online(&cycle, false);
    assert(dpi_cycle_current(&cycle) == 0);
    assert(dpi_cycle_next(&cycle) == 0);

    /* Device reconnects: restores state and resets index to 0 */
    dpi_cycle_set_online(&cycle, true);
    assert(dpi_cycle_current(&cycle) == 800);
}

static void test_dpi_boundary_clamping(void)
{
    uint16_t min_dpi = 200;
    uint16_t max_dpi = 8000;
    uint16_t step = 50;

    /* Below minimum -> clamped to min */
    assert(dpi_clamp_sensor(100, min_dpi, max_dpi, step) == 200);

    /* Above maximum -> clamped to max */
    assert(dpi_clamp_sensor(9500, min_dpi, max_dpi, step) == 8000);

    /* Step snapping: 1234 -> 1250 (nearest 50) */
    assert(dpi_clamp_sensor(1234, min_dpi, max_dpi, step) == 1250);

    /* Step snapping: 1220 -> 1200 (nearest 50) */
    assert(dpi_clamp_sensor(1220, min_dpi, max_dpi, step) == 1200);

    /* Exact boundaries */
    assert(dpi_clamp_sensor(200, min_dpi, max_dpi, step) == 200);
    assert(dpi_clamp_sensor(8000, min_dpi, max_dpi, step) == 8000);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_dpi_cycle", "DPI Preset Cycling & Sensor Bounds Clamping");

    TEST_CASE_RUN(g_suite, test_dpi_cycle_advancement_and_wrap);
    TEST_CASE_RUN(g_suite, test_dpi_cycle_offline_disconnect_and_restore);
    TEST_CASE_RUN(g_suite, test_dpi_boundary_clamping);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
