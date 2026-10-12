/**
 * @file test_gesture_and_inject.c
 * @brief Unit tests for gesture classification, swipe accumulator, and synthetic input injection.
 */

#include "gesture.h"
#include "inject.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

TEST_SUITE_DECLARE(g_suite);

static void test_detect_swipe_thresholds(void)
{
    /* Below minimum threshold (50) */
    assert(detect_swipe(40, 0) == GESTURE_DIR_NONE);
    assert(detect_swipe(-40, 0) == GESTURE_DIR_NONE);
    assert(detect_swipe(0, 40) == GESTURE_DIR_NONE);
    assert(detect_swipe(0, -40) == GESTURE_DIR_NONE);

    /* Clean cardinal directions passing threshold */
    assert(detect_swipe(60, 0) == GESTURE_DIR_RIGHT);
    assert(detect_swipe(-60, 0) == GESTURE_DIR_LEFT);
    assert(detect_swipe(0, -60) == GESTURE_DIR_UP);
    assert(detect_swipe(0, 60) == GESTURE_DIR_DOWN);

    /* Acceptable slight diagonal (cross axis within deadzone) */
    assert(detect_swipe(100, 30) == GESTURE_DIR_RIGHT);
    assert(detect_swipe(-100, -30) == GESTURE_DIR_LEFT);
    assert(detect_swipe(25, -120) == GESTURE_DIR_UP);
    assert(detect_swipe(-30, 120) == GESTURE_DIR_DOWN);

    /* Excessive diagonal exceeding cross limit -> rejected */
    assert(detect_swipe(100, 80) == GESTURE_DIR_NONE);
    assert(detect_swipe(80, 100) == GESTURE_DIR_NONE);
    assert(detect_swipe(-100, -90) == GESTURE_DIR_NONE);
}

static void test_swipe_accumulator_commit_once(void)
{
    swipe_accumulator_t acc;
    swipe_accumulator_init(&acc);

    assert(swipe_accumulator_is_holding(&acc) == false);

    /* Press down */
    swipe_accumulator_begin(&acc);
    assert(swipe_accumulator_is_holding(&acc) == true);

    /* Small motion before hold gate */
    gesture_direction_t dir = swipe_accumulator_accumulate(&acc, 20, 0);
    assert(dir == GESTURE_DIR_NONE);

    /* Backdate hold time past 160ms threshold */
    swipe_accumulator_backdate_for_test(&acc, 200);

    /* Exceed threshold -> commits to RIGHT */
    dir = swipe_accumulator_accumulate(&acc, 50, 0);
    assert(dir == GESTURE_DIR_RIGHT);

    /* Further motion must NOT fire a second time during same hold */
    dir = swipe_accumulator_accumulate(&acc, 80, 0);
    assert(dir == GESTURE_DIR_NONE);

    /* Release button: must NOT report click since swipe already fired */
    bool was_click = swipe_accumulator_end(&acc);
    assert(was_click == false);
    assert(swipe_accumulator_is_holding(&acc) == false);
}

static void test_swipe_accumulator_plain_click(void)
{
    swipe_accumulator_t acc;
    swipe_accumulator_init(&acc);

    swipe_accumulator_begin(&acc);

    /* Minor cursor drift below threshold */
    gesture_direction_t dir = swipe_accumulator_accumulate(&acc, 5, 2);
    assert(dir == GESTURE_DIR_NONE);

    /* Release button -> must report plain click */
    bool was_click = swipe_accumulator_end(&acc);
    assert(was_click == true);
    assert(swipe_accumulator_is_holding(&acc) == false);
}

static void test_swipe_accumulator_hold_time_gate(void)
{
    swipe_accumulator_t acc;
    swipe_accumulator_init(&acc);

    swipe_accumulator_begin(&acc);

    /* Large motion immediately (hold time < 160ms) -> must NOT commit prematurely */
    gesture_direction_t dir = swipe_accumulator_accumulate(&acc, 0, -100);
    assert(dir == GESTURE_DIR_NONE);

    /* Now satisfy hold duration gate */
    swipe_accumulator_backdate_for_test(&acc, 180);

    /* Next sample commits the pending UP gesture */
    dir = swipe_accumulator_accumulate(&acc, 0, -1);
    assert(dir == GESTURE_DIR_UP);

    bool was_click = swipe_accumulator_end(&acc);
    assert(was_click == false);
}

static void test_gesture_direction_names(void)
{
    assert(strcmp(gesture_direction_name(GESTURE_DIR_UP), "Up") == 0);
    assert(strcmp(gesture_direction_name(GESTURE_DIR_DOWN), "Down") == 0);
    assert(strcmp(gesture_direction_name(GESTURE_DIR_LEFT), "Left") == 0);
    assert(strcmp(gesture_direction_name(GESTURE_DIR_RIGHT), "Right") == 0);
    assert(strcmp(gesture_direction_name(GESTURE_DIR_CLICK), "Click") == 0);
    assert(strcmp(gesture_direction_name(GESTURE_DIR_NONE), "None") == 0);
}

static void test_input_injection_execution(void)
{
    bool ok = inject_init();
    assert(ok == true);

    /* Test action dispatch structures safely */
    openlogi_action_t act;
    memset(&act, 0, sizeof(act));

    act.type = ACTION_NONE;
    inject_action(&act);

    act.type = ACTION_MISSION_CONTROL;
    inject_action(&act);

    act.type = ACTION_APP_EXPOSE;
    inject_action(&act);

    act.type = ACTION_SHOW_DESKTOP;
    inject_action(&act);

    act.type = ACTION_MEDIA_KEY;
    act.u.media_key = MEDIA_KEY_PLAY_PAUSE;
    inject_action(&act);

    act.type = ACTION_SCROLL;
    act.u.scroll.dx = 0;
    act.u.scroll.dy = 5;
    inject_action(&act);
}

int main(void)
{
    test_suite_begin(&g_suite, "test_gesture_and_inject", "Gesture Swipe Accumulator & Native Injection");

    TEST_CASE_RUN(g_suite, test_detect_swipe_thresholds);
    TEST_CASE_RUN(g_suite, test_swipe_accumulator_commit_once);
    TEST_CASE_RUN(g_suite, test_swipe_accumulator_plain_click);
    TEST_CASE_RUN(g_suite, test_swipe_accumulator_hold_time_gate);
    TEST_CASE_RUN(g_suite, test_gesture_direction_names);
    TEST_CASE_RUN(g_suite, test_input_injection_execution);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
