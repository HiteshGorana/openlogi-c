/**
 * @file test_button_runtime.c
 * @brief Regression tests for button runtime state machine ported from openlogi-agent-core.
 */

#include "button_runtime.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define BTN_BACK    0x0053
#define BTN_FORWARD 0x0056

typedef struct {
    size_t started_count;
    size_t triggered_short_count;
    size_t triggered_long_count;
    size_t ended_released_count;
    size_t ended_canceled_repress_count;
    size_t ended_canceled_invalidated_count;
    size_t ended_canceled_stale_count;
    uint64_t last_triggered_press_id;
} test_recorder_t;

static void on_test_event(const button_runtime_event_t *ev, void *user_data)
{
    test_recorder_t *rec = (test_recorder_t *)user_data;
    if (ev->type == BUTTON_EVT_STARTED) {
        rec->started_count++;
    } else if (ev->type == BUTTON_EVT_TRIGGERED) {
        rec->last_triggered_press_id = ev->press.press_id;
        if (ev->action_type == TRIGGER_ACTION_SHORT_CLICK) {
            rec->triggered_short_count++;
        } else if (ev->action_type == TRIGGER_ACTION_LONG_PRESS) {
            rec->triggered_long_count++;
        }
    } else if (ev->type == BUTTON_EVT_ENDED) {
        if (ev->end_reason == BUTTON_END_RELEASED) {
            rec->ended_released_count++;
        } else if (ev->end_reason == BUTTON_END_CANCELED_REPEATED_DOWN) {
            rec->ended_canceled_repress_count++;
        } else if (ev->end_reason == BUTTON_END_CANCELED_INVALIDATED) {
            rec->ended_canceled_invalidated_count++;
        } else if (ev->end_reason == BUTTON_END_CANCELED_STALE_HOLD) {
            rec->ended_canceled_stale_count++;
        }
    }
}

static void test_button_release_idempotency(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));
    assert(button_runtime_is_pressed(&rt, BTN_BACK));
    assert(rec.started_count == 1);

    /* First release succeeds */
    assert(button_runtime_up(&rt, BTN_BACK, 1100));
    assert(!button_runtime_is_pressed(&rt, BTN_BACK));
    assert(rec.ended_released_count == 1);
    assert(rec.triggered_short_count == 1);

    /* Second release fails (idempotent: does not trigger again) */
    assert(!button_runtime_up(&rt, BTN_BACK, 1200));
    assert(rec.ended_released_count == 1);
    assert(rec.triggered_short_count == 1);
}

static void test_button_repress_replaces_old(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));
    assert(rec.started_count == 1);

    /* Second down without release: cancels old press and starts new */
    assert(button_runtime_down(&rt, BTN_BACK, false, 1100, NULL));
    assert(rec.started_count == 2);
    assert(rec.ended_canceled_repress_count == 1);

    /* Release triggers the new press */
    assert(button_runtime_up(&rt, BTN_BACK, 1200));
    assert(rec.triggered_short_count == 1);
    assert(rec.ended_released_count == 1);
}

static void test_button_short_click_timing(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    /* Press down at t=1000 */
    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));

    /* Tick at t=1200 (200ms elapsed, less than 300ms threshold) */
    button_runtime_tick(&rt, 1200);
    assert(rec.triggered_long_count == 0);
    assert(rec.triggered_short_count == 0);

    /* Release at t=1250 (250ms elapsed): triggers short click */
    assert(button_runtime_up(&rt, BTN_BACK, 1250));
    assert(rec.triggered_short_count == 1);
    assert(rec.triggered_long_count == 0);
}

static void test_button_long_press_threshold_commit_once(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    /* Press down at t=1000 */
    assert(button_runtime_down(&rt, BTN_BACK, true, 1000, NULL));

    /* Tick past threshold at t=1301 (301ms elapsed): long press triggers */
    button_runtime_tick(&rt, 1301);
    assert(rec.triggered_long_count == 1);
    assert(rec.triggered_short_count == 0);

    /* Subsequent ticks do not trigger again */
    button_runtime_tick(&rt, 1400);
    button_runtime_tick(&rt, 1500);
    assert(rec.triggered_long_count == 1);

    /* Release suppresses short click because long press already fired */
    assert(button_runtime_up(&rt, BTN_BACK, 1600));
    assert(rec.triggered_short_count == 0);
    assert(rec.ended_released_count == 1);
}

static void test_button_cancellation_safety(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));
    assert(button_runtime_is_pressed(&rt, BTN_BACK));

    /* Invalidate active button */
    assert(button_runtime_cancel(&rt, BTN_BACK, BUTTON_END_CANCELED_INVALIDATED));
    assert(!button_runtime_is_pressed(&rt, BTN_BACK));
    assert(rec.ended_canceled_invalidated_count == 1);

    /* Release after cancellation must be a no-op */
    assert(!button_runtime_up(&rt, BTN_BACK, 1100));
    assert(rec.triggered_short_count == 0);
    assert(rec.triggered_long_count == 0);
    assert(rec.ended_released_count == 0);
}

static void test_button_stale_hold_timeout(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    /* Press down at t=1000 */
    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));

    /* Tick past stale threshold (t=1000 + 30000ms + 1 = 31001) */
    button_runtime_tick(&rt, 31001);
    assert(!button_runtime_is_pressed(&rt, BTN_BACK));
    assert(rec.ended_canceled_stale_count == 1);
}

static void test_button_cancel_all(void)
{
    test_recorder_t rec = { 0 };
    button_runtime_t rt;
    button_runtime_init(&rt, on_test_event, &rec);

    assert(button_runtime_down(&rt, BTN_BACK, false, 1000, NULL));
    assert(button_runtime_down(&rt, BTN_FORWARD, true, 1010, NULL));
    assert(button_runtime_active_count(&rt) == 2);

    button_runtime_cancel_all(&rt, BUTTON_END_CANCELED_INVALIDATED);
    assert(button_runtime_active_count(&rt) == 0);
    assert(rec.ended_canceled_invalidated_count == 2);
    assert(rec.triggered_short_count == 0);
    assert(rec.triggered_long_count == 0);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_button_runtime", "Button Runtime & Press Lifecycle");

    TEST_CASE_RUN(g_suite, test_button_release_idempotency);
    TEST_CASE_RUN(g_suite, test_button_repress_replaces_old);
    TEST_CASE_RUN(g_suite, test_button_short_click_timing);
    TEST_CASE_RUN(g_suite, test_button_long_press_threshold_commit_once);
    TEST_CASE_RUN(g_suite, test_button_cancellation_safety);
    TEST_CASE_RUN(g_suite, test_button_stale_hold_timeout);
    TEST_CASE_RUN(g_suite, test_button_cancel_all);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
