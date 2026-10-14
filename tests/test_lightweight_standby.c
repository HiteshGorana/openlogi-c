/**
 * @file test_lightweight_standby.c
 * @brief Unit and regression tests for lightweight standby state transitions and cleanup.
 */

#include "daemon_state.h"
#include "ipc_server.h"
#include "action_queue.h"
#include "button_runtime.h"
#include "gesture.h"
#include "inject.h"
#include "yyjson.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

TEST_SUITE_DECLARE(g_suite);

static void test_state_computation(void)
{
    /* 0 devices -> STANDBY */
    assert(daemon_compute_target_state(0) == RUNTIME_STATE_STANDBY);

    /* 1 or more devices -> ACTIVE */
    assert(daemon_compute_target_state(1) == RUNTIME_STATE_ACTIVE);
    assert(daemon_compute_target_state(2) == RUNTIME_STATE_ACTIVE);
    assert(daemon_compute_target_state(10) == RUNTIME_STATE_ACTIVE);

    /* Transitions */
    assert(!daemon_state_should_transition(RUNTIME_STATE_STANDBY, RUNTIME_STATE_STANDBY));
    assert(daemon_state_should_transition(RUNTIME_STATE_STANDBY, RUNTIME_STATE_ACTIVE));
    assert(daemon_state_should_transition(RUNTIME_STATE_ACTIVE, RUNTIME_STATE_STANDBY));
    assert(!daemon_state_should_transition(RUNTIME_STATE_ACTIVE, RUNTIME_STATE_ACTIVE));

    /* String representations */
    assert(strcmp(daemon_state_to_string(RUNTIME_STATE_STANDBY), "standby") == 0);
    assert(strcmp(daemon_state_to_string(RUNTIME_STATE_ACTIVE), "active") == 0);
}

static void test_standby_initial_state(void)
{
    openlogi_daemon_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* At startup with zero devices probed, runtime_state is STANDBY */
    ctx.runtime_state = RUNTIME_STATE_STANDBY;
    assert(ctx.runtime_state == RUNTIME_STATE_STANDBY);
    assert(daemon_compute_target_state(ctx.device_count) == RUNTIME_STATE_STANDBY);
    assert(!daemon_state_should_transition(ctx.runtime_state, RUNTIME_STATE_STANDBY));
}

static void test_action_queue_clear_pending_on_disconnect(void)
{
    inject_set_dry_run(true);
    assert(action_queue_init());

    /* Enqueue multiple blocking shell actions */
    openlogi_action_t act;
    memset(&act, 0, sizeof(act));
    act.type = ACTION_SHELL_COMMAND;
    snprintf(act.u.shell_cmd, sizeof(act.u.shell_cmd), "sleep 1");

    for (int i = 0; i < 5; i++) {
        action_queue_enqueue(&act);
    }

    /* Verify clear pending empties queue immediately without waiting for commands */
    uint64_t t0 = test_now_ns();
    action_queue_clear_pending();
    uint64_t t1 = test_now_ns();
    double ms = (double)(t1 - t0) / 1000000.0;

    /* Clearing should be virtually instantaneous (< 50 ms) */
    assert(ms < 50.0);
    assert(action_queue_pending_count() == 0);

    action_queue_shutdown();
    inject_set_dry_run(false);
}

static void test_state_transition_multi_device(void)
{
    openlogi_daemon_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.runtime_state = RUNTIME_STATE_STANDBY;

    /* Case 1: Start with 0 devices -> STANDBY */
    size_t online_devices = 0;
    openlogi_runtime_state_t target = daemon_compute_target_state(online_devices);
    assert(target == RUNTIME_STATE_STANDBY);
    assert(!daemon_state_should_transition(ctx.runtime_state, target));

    /* Case 2: Connect first device -> Transition to ACTIVE */
    online_devices = 1;
    target = daemon_compute_target_state(online_devices);
    assert(target == RUNTIME_STATE_ACTIVE);
    assert(daemon_state_should_transition(ctx.runtime_state, target));
    ctx.runtime_state = target;

    /* Case 3: Connect second device -> Stays ACTIVE (no transition) */
    online_devices = 2;
    target = daemon_compute_target_state(online_devices);
    assert(target == RUNTIME_STATE_ACTIVE);
    assert(!daemon_state_should_transition(ctx.runtime_state, target));

    /* Case 4: Disconnect one device (1 remains online) -> Stays ACTIVE */
    online_devices = 1;
    target = daemon_compute_target_state(online_devices);
    assert(target == RUNTIME_STATE_ACTIVE);
    assert(!daemon_state_should_transition(ctx.runtime_state, target));

    /* Case 5: Disconnect last device -> Transition to STANDBY */
    online_devices = 0;
    target = daemon_compute_target_state(online_devices);
    assert(target == RUNTIME_STATE_STANDBY);
    assert(daemon_state_should_transition(ctx.runtime_state, target));
    ctx.runtime_state = target;
}

static void test_diversion_safety_invariants(void)
{
    /* On transition to standby, buttons and gestures must be cleared */
    button_runtime_t rt;
    button_runtime_init(&rt, NULL, NULL);

    /* Simulate a button held down */
    button_runtime_down(&rt, 1, false, 1000, NULL);
    assert(button_runtime_is_pressed(&rt, 1));
    assert(button_runtime_active_count(&rt) == 1);

    /* Disconnect cancels all pending button states */
    button_runtime_cancel_all(&rt, BUTTON_END_CANCELED_INVALIDATED);
    assert(!button_runtime_is_pressed(&rt, 1));
    assert(button_runtime_active_count(&rt) == 0);

    /* Gesture accumulator reset */
    swipe_accumulator_t acc;
    swipe_accumulator_init(&acc);
    acc.accum_dx = 15;
    acc.accum_dy = 20;
    assert(acc.accum_dx != 0 || acc.accum_dy != 0);

    swipe_accumulator_init(&acc);
    assert(acc.accum_dx == 0 && acc.accum_dy == 0);
}

static void test_ipc_get_status_runtime_state(void)
{
    openlogi_daemon_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.start_time = time(NULL) - 60;
    ctx.config.schema_version = 7;
    ctx.runtime_state = RUNTIME_STATE_STANDBY;

    /* 1. Request status in STANDBY */
    const char *req = "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"openlogi.get_status\",\"params\":{}}";
    char *resp = ipc_dispatch_jsonrpc(&ctx, req);
    assert(resp != NULL);

    yyjson_doc *doc = yyjson_read(resp, strlen(resp), 0);
    assert(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *res = yyjson_obj_get(root, "result");
    assert(res != NULL);

    const char *state_str = yyjson_get_str(yyjson_obj_get(res, "runtime_state"));
    assert(state_str != NULL);
    assert(strcmp(state_str, "standby") == 0);

    yyjson_doc_free(doc);
    free(resp);

    /* 2. Request status in ACTIVE */
    ctx.runtime_state = RUNTIME_STATE_ACTIVE;
    resp = ipc_dispatch_jsonrpc(&ctx, req);
    assert(resp != NULL);

    doc = yyjson_read(resp, strlen(resp), 0);
    assert(doc != NULL);
    root = yyjson_doc_get_root(doc);
    res = yyjson_obj_get(root, "result");
    assert(res != NULL);

    state_str = yyjson_get_str(yyjson_obj_get(res, "runtime_state"));
    assert(state_str != NULL);
    assert(strcmp(state_str, "active") == 0);

    yyjson_doc_free(doc);
    free(resp);
}

static void test_standby_reconcile_idempotence(void)
{
    openlogi_daemon_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.runtime_state = RUNTIME_STATE_STANDBY;

    /* Repeatedly check standby state */
    for (int i = 0; i < 50; i++) {
        openlogi_runtime_state_t target = daemon_compute_target_state(0);
        assert(!daemon_state_should_transition(ctx.runtime_state, target));
    }

    ctx.runtime_state = RUNTIME_STATE_ACTIVE;

    /* Repeatedly check active state */
    for (int i = 0; i < 50; i++) {
        openlogi_runtime_state_t target = daemon_compute_target_state(1);
        assert(!daemon_state_should_transition(ctx.runtime_state, target));
    }
}

int main(void)
{
    test_suite_begin(&g_suite, "test_lightweight_standby", "Lightweight Standby State Transitions & Safety Invariants");

    TEST_CASE_RUN(g_suite, test_state_computation);
    TEST_CASE_RUN(g_suite, test_standby_initial_state);
    TEST_CASE_RUN(g_suite, test_action_queue_clear_pending_on_disconnect);
    TEST_CASE_RUN(g_suite, test_state_transition_multi_device);
    TEST_CASE_RUN(g_suite, test_diversion_safety_invariants);
    TEST_CASE_RUN(g_suite, test_ipc_get_status_runtime_state);
    TEST_CASE_RUN(g_suite, test_standby_reconcile_idempotence);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
