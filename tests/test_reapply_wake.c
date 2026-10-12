/**
 * @file test_reapply_wake.c
 * @brief Regression tests for device re-apply transitions ported from openlogi-agent-core.
 */

#include "device_reapply.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static reapply_device_t make_dev(const char *key, uint8_t slot, bool online)
{
    reapply_device_t dev;
    memset(&dev, 0, sizeof(dev));
    strncpy(dev.device_key, key, sizeof(dev.device_key) - 1);
    dev.slot = slot;
    dev.online = online;
    return dev;
}

static void test_reapply_targets_new_arrivals_and_transitions(void)
{
    reapply_plan_t plan;

    /* 1. First sighting of an online device -> re-apply */
    reapply_device_t d1 = make_dev("a", 1, true);
    device_reapply_plan(NULL, 0, &d1, 1, false, &plan);
    assert(plan.target_count == 1);
    assert(reapply_plan_contains(&plan, 0));

    /* 2. Steady state -> nothing */
    device_reapply_plan(&d1, 1, &d1, 1, false, &plan);
    assert(plan.target_count == 0);

    /* 3. Replug under a new route (same key, new slot 2) -> re-apply */
    reapply_device_t d1_slot2 = make_dev("a", 2, true);
    device_reapply_plan(&d1, 1, &d1_slot2, 1, false, &plan);
    assert(plan.target_count == 1);
    assert(reapply_plan_contains(&plan, 0));

    /* 4. Waking from device sleep (offline -> online) -> re-apply */
    reapply_device_t d1_offline = make_dev("a", 1, false);
    device_reapply_plan(&d1_offline, 1, &d1, 1, false, &plan);
    assert(plan.target_count == 1);
    assert(reapply_plan_contains(&plan, 0));

    /* 5. Going to sleep (online -> offline) -> nothing (never write to sleeping hardware) */
    device_reapply_plan(&d1, 1, &d1_offline, 1, false, &plan);
    assert(plan.target_count == 0);
}

static void test_reapply_targets_disambiguates_same_model_duplicates(void)
{
    reapply_plan_t plan;

    /* Two physical mice sharing the same model key at distinct slots */
    reapply_device_t prev[2] = { make_dev("dup", 1, true), make_dev("dup", 2, true) };
    reapply_device_t next[2] = { make_dev("dup", 1, true), make_dev("dup", 2, true) };

    /* Steady tick must target NEITHER */
    device_reapply_plan(prev, 2, next, 2, false, &plan);
    assert(plan.target_count == 0);
}

static void test_reapply_targets_skip_offline_and_sleeping(void)
{
    reapply_plan_t plan;

    /* Paired but asleep new arrival: must skip until online transition */
    reapply_device_t asleep = make_dev("a", 1, false);
    device_reapply_plan(NULL, 0, &asleep, 1, false, &plan);
    assert(plan.target_count == 0);
}

static void test_reapply_all_targets_every_online_device(void)
{
    reapply_plan_t plan;

    reapply_device_t prev[2] = { make_dev("a", 1, true), make_dev("b", 2, false) };
    reapply_device_t next[2] = { make_dev("a", 1, true), make_dev("b", 2, false) };

    /* Post-wake snapshot with is_system_wake = true: targets online device [0], skips offline [1] */
    device_reapply_plan(prev, 2, next, 2, true, &plan);
    assert(plan.target_count == 1);
    assert(reapply_plan_contains(&plan, 0));
    assert(!reapply_plan_contains(&plan, 1));
}

static void test_system_wake_with_multiple_devices(void)
{
    reapply_plan_t plan;

    reapply_device_t devs[2] = { make_dev("mouse", 1, true), make_dev("keyboard", 2, true) };

    /* Host wakes from sleep: both online devices must be targeted for re-arm */
    device_reapply_plan(devs, 2, devs, 2, true, &plan);
    assert(plan.target_count == 2);
    assert(reapply_plan_contains(&plan, 0));
    assert(reapply_plan_contains(&plan, 1));
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_reapply_wake", "Device Re-Apply & Sleep/Wake Transitions");

    TEST_CASE_RUN(g_suite, test_reapply_targets_new_arrivals_and_transitions);
    TEST_CASE_RUN(g_suite, test_reapply_targets_disambiguates_same_model_duplicates);
    TEST_CASE_RUN(g_suite, test_reapply_targets_skip_offline_and_sleeping);
    TEST_CASE_RUN(g_suite, test_reapply_all_targets_every_online_device);
    TEST_CASE_RUN(g_suite, test_system_wake_with_multiple_devices);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
