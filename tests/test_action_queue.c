/**
 * @file test_action_queue.c
 * @brief Unit tests for asynchronous non-blocking action execution queue.
 */

#include "action_queue.h"
#include "inject.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

TEST_SUITE_DECLARE(g_suite);

static void test_action_queue_lifecycle(void)
{
    assert(action_queue_init());
    /* Double init is safe */
    assert(action_queue_init());
    action_queue_shutdown();
    /* Double shutdown is safe */
    action_queue_shutdown();
}

static void test_action_queue_blocking_detection(void)
{
    openlogi_action_t act;
    memset(&act, 0, sizeof(act));

    act.type = ACTION_KEY_COMBO;
    assert(!action_queue_is_blocking(&act));

    act.type = ACTION_MISSION_CONTROL;
    assert(!action_queue_is_blocking(&act));

    act.type = ACTION_MOUSE_CLICK;
    assert(!action_queue_is_blocking(&act));

    act.type = ACTION_SCROLL;
    assert(!action_queue_is_blocking(&act));

    act.type = ACTION_WORKFLOW;
    assert(action_queue_is_blocking(&act));

    act.type = ACTION_SHELL_COMMAND;
    assert(action_queue_is_blocking(&act));

    act.type = ACTION_APP_OPEN;
    assert(action_queue_is_blocking(&act));

    act.type = ACTION_APPLE_SCRIPT;
    assert(action_queue_is_blocking(&act));

    act.type = ACTION_SLEEP;
    assert(action_queue_is_blocking(&act));
}

static void test_action_queue_async_workflow_execution(void)
{
    inject_set_dry_run(true);
    assert(action_queue_init());

    openlogi_action_t act;
    memset(&act, 0, sizeof(act));
    act.type = ACTION_WORKFLOW;
    act.u.workflow = (openlogi_workflow_t *)calloc(1, sizeof(openlogi_workflow_t));
    assert(act.u.workflow);

    act.u.workflow->step_count = 2;
    act.u.workflow->steps[0].type = WORKFLOW_STEP_PRESS_KEY;
    act.u.workflow->steps[0].u.key.keycode = 0x09;
    act.u.workflow->steps[0].u.key.modifiers = MOD_CMD;
    act.u.workflow->steps[1].type = WORKFLOW_STEP_DELAY;
    act.u.workflow->steps[1].u.delay_ms = 10;

    /* Enqueue action */
    bool ok = action_queue_enqueue(&act);
    assert(ok);

    /* Free caller's workflow immediately: tests deep-copy memory isolation */
    free(act.u.workflow);
    act.u.workflow = NULL;

    /* Wait for worker to finish executing */
    action_queue_wait_idle();
    assert(action_queue_pending_count() == 0);

    action_queue_shutdown();
    inject_set_dry_run(false);
}

static void test_action_queue_multiple_enqueues(void)
{
    inject_set_dry_run(true);
    assert(action_queue_init());

    for (int i = 0; i < 10; i++) {
        openlogi_action_t act;
        memset(&act, 0, sizeof(act));
        act.type = ACTION_SHELL_COMMAND;
        snprintf(act.u.shell_cmd, sizeof(act.u.shell_cmd), "echo test %d", i);
        assert(action_queue_enqueue(&act));
    }

    action_queue_wait_idle();
    assert(action_queue_pending_count() == 0);

    action_queue_shutdown();
    inject_set_dry_run(false);
}

int main(void)
{
    test_suite_begin(&g_suite, "test_action_queue", "Asynchronous Non-Blocking Action Queue");

    TEST_CASE_RUN(g_suite, test_action_queue_lifecycle);
    TEST_CASE_RUN(g_suite, test_action_queue_blocking_detection);
    TEST_CASE_RUN(g_suite, test_action_queue_async_workflow_execution);
    TEST_CASE_RUN(g_suite, test_action_queue_multiple_enqueues);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
