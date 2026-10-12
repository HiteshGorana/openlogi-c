/**
 * @file test_capture_plan.c
 * @brief Regression tests for capture plan resolution ported from openlogi-agent-core.
 */

#include "capture_plan.h"
#include "config.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void test_capture_plan_resolves_standard_buttons(void)
{
    config_device_t dev;
    memset(&dev, 0, sizeof(dev));

    /* Map Back button */
    dev.default_profile.bindings[0].button_id = CONFIG_BUTTON_BACK;
    dev.default_profile.binding_count = 1;

    capture_spec_t spec;
    capture_plan_resolve(&dev, &spec);

    assert(spec.divert_count == 1);
    assert(capture_spec_has_cid(&spec, CID_BACK_BUTTON));
    assert(!capture_spec_has_cid(&spec, CID_FORWARD_BUTTON));
    assert(!spec.wants_raw_xy);
}

static void test_capture_plan_gesture_button_includes_xy(void)
{
    config_device_t dev;
    memset(&dev, 0, sizeof(dev));

    /* Map gesture button */
    dev.default_profile.bindings[0].button_id = CONFIG_BUTTON_GESTURE;
    dev.default_profile.bindings[0].is_gesture = true;
    dev.default_profile.binding_count = 1;

    capture_spec_t spec;
    capture_plan_resolve(&dev, &spec);

    assert(spec.divert_count == 1);
    assert(capture_spec_has_cid(&spec, CID_GESTURE_BUTTON));
    assert(spec.wants_raw_xy == true);
}

static void test_capture_plan_per_app_override_merging(void)
{
    config_device_t dev;
    memset(&dev, 0, sizeof(dev));

    /* Global default has only Forward mapped */
    dev.default_profile.bindings[0].button_id = CONFIG_BUTTON_FORWARD;
    dev.default_profile.binding_count = 1;

    /* Per-app profile for VSCode has Back mapped */
    strcpy(dev.app_profiles[0].app_id, "com.microsoft.VSCode");
    dev.app_profiles[0].bindings[0].button_id = CONFIG_BUTTON_BACK;
    dev.app_profiles[0].binding_count = 1;
    dev.app_profile_count = 1;

    capture_spec_t spec;
    capture_plan_resolve(&dev, &spec);

    /* Hardware diversion must union both Forward and Back */
    assert(spec.divert_count == 2);
    assert(capture_spec_has_cid(&spec, CID_FORWARD_BUTTON));
    assert(capture_spec_has_cid(&spec, CID_BACK_BUTTON));
}

static void test_capture_plan_unmapped_buttons_remain_hardware(void)
{
    config_device_t dev;
    memset(&dev, 0, sizeof(dev));

    /* Only MiddleClick mapped */
    dev.default_profile.bindings[0].button_id = CONFIG_BUTTON_MIDDLE;
    dev.default_profile.binding_count = 1;

    capture_spec_t spec;
    capture_plan_resolve(&dev, &spec);

    assert(spec.divert_count == 1);
    assert(capture_spec_has_cid(&spec, CID_MIDDLE_BUTTON));
    assert(!capture_spec_has_cid(&spec, CID_BACK_BUTTON));
    assert(!capture_spec_has_cid(&spec, CID_FORWARD_BUTTON));
    assert(!capture_spec_has_cid(&spec, CID_GESTURE_BUTTON));
}

static void test_capture_spec_rearm_equality(void)
{
    capture_spec_t s1, s2;
    capture_spec_init(&s1);
    capture_spec_init(&s2);

    capture_spec_add_cid(&s1, CID_BACK_BUTTON);
    capture_spec_add_cid(&s2, CID_BACK_BUTTON);

    /* Equal specs do not need rearm */
    assert(!capture_spec_needs_rearm(&s1, &s2));

    /* Changing raw_xy triggers rearm */
    s2.wants_raw_xy = true;
    assert(capture_spec_needs_rearm(&s1, &s2));
    s2.wants_raw_xy = false;

    /* Adding another CID triggers rearm */
    capture_spec_add_cid(&s2, CID_FORWARD_BUTTON);
    assert(capture_spec_needs_rearm(&s1, &s2));
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_capture_plan", "Capture Plan Resolution & Hardware Diversion Spec");

    TEST_CASE_RUN(g_suite, test_capture_plan_resolves_standard_buttons);
    TEST_CASE_RUN(g_suite, test_capture_plan_gesture_button_includes_xy);
    TEST_CASE_RUN(g_suite, test_capture_plan_per_app_override_merging);
    TEST_CASE_RUN(g_suite, test_capture_plan_unmapped_buttons_remain_hardware);
    TEST_CASE_RUN(g_suite, test_capture_spec_rearm_equality);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
