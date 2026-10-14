/**
 * @file test_diversion_and_hook.c
 * @brief Unit tests for ReprogControls 0x1B04 diversion, safe rollback, and input hook.
 */

#include "hidpp.h"
#include "hidpp_reprog.h"
#include "hid_mock.h"
#include "hook.h"
#include "signals.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_DEVICE_INDEX 0xFF
#define TEST_SW_ID        0x02
#define MOCK_FEATURE_IDX_REPROG 0x06

TEST_SUITE_DECLARE(g_suite);

static void test_reprog_get_count(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    assert(dev != NULL);

    uint8_t count = 0;
    bool ok = hidpp_reprog_get_count(dev, TEST_DEVICE_INDEX, MOCK_FEATURE_IDX_REPROG, TEST_SW_ID, &count);
    assert(ok == true);
    assert(count == 3);

    hid_mock_destroy(dev);
}

static void test_reprog_get_cid_info(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    assert(dev != NULL);

    hidpp_cid_info_t info;
    bool ok = hidpp_reprog_get_cid_info(dev, TEST_DEVICE_INDEX, MOCK_FEATURE_IDX_REPROG, TEST_SW_ID, 0, &info);
    assert(ok == true);
    assert(info.cid == HIDPP_CID_GESTURE_BUTTON);
    assert(info.flags & HIDPP_CID_FLAG_DIVERTABLE);
    assert(info.flags & HIDPP_CID_FLAG_RAW_XY);

    /* Back button */
    ok = hidpp_reprog_get_cid_info(dev, TEST_DEVICE_INDEX, MOCK_FEATURE_IDX_REPROG, TEST_SW_ID, 1, &info);
    assert(ok == true);
    assert(info.cid == HIDPP_CID_BACK_CLASSIC);
    assert(info.flags & HIDPP_CID_FLAG_DIVERTABLE);
    assert(!(info.flags & HIDPP_CID_FLAG_RAW_XY));

    hid_mock_destroy(dev);
}

static void test_reprog_set_and_get_reporting(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    assert(dev != NULL);

    /* Divert gesture button with raw XY */
    bool ok = hidpp_reprog_set_reporting(
        dev,
        TEST_DEVICE_INDEX,
        MOCK_FEATURE_IDX_REPROG,
        TEST_SW_ID,
        HIDPP_CID_GESTURE_BUTTON,
        true,  /* divert */
        true   /* raw_xy */
    );
    assert(ok == true);

    /* Undivert back button */
    ok = hidpp_reprog_set_reporting(
        dev,
        TEST_DEVICE_INDEX,
        MOCK_FEATURE_IDX_REPROG,
        TEST_SW_ID,
        HIDPP_CID_BACK_CLASSIC,
        false, /* undivert */
        false
    );
    assert(ok == true);

    hid_mock_destroy(dev);
}

static void test_reprog_event_decoding(void)
{
    /* 1. Test DivertedButtons event (Function 0) */
    hidpp_v20_msg_t btn_msg;
    memset(&btn_msg, 0, sizeof(btn_msg));
    btn_msg.report_id = HIDPP_REPORT_LONG;
    btn_msg.device_index = TEST_DEVICE_INDEX;
    btn_msg.feature_index = MOCK_FEATURE_IDX_REPROG;
    btn_msg.function_id = 0x00;
    btn_msg.software_id = 0x00; /* Unsolicited notification */
    btn_msg.payload_len = 8;
    /* CID 1: 0x00C3, CID 2: 0x0053, CID 3: 0, CID 4: 0 */
    btn_msg.payload[0] = 0x00; btn_msg.payload[1] = 0xC3;
    btn_msg.payload[2] = 0x00; btn_msg.payload[3] = 0x53;

    hidpp_reprog_event_t event;
    bool ok = hidpp_reprog_decode_event(&btn_msg, MOCK_FEATURE_IDX_REPROG, &event);
    assert(ok == true);
    assert(event.type == REPROG_EVENT_DIVERTED_BUTTONS);
    assert(event.u.buttons.count == 2);
    assert(event.u.buttons.pressed_cids[0] == HIDPP_CID_GESTURE_BUTTON);
    assert(event.u.buttons.pressed_cids[1] == HIDPP_CID_BACK_CLASSIC);

    /* 2. Test RawXY event (Function 1) */
    hidpp_v20_msg_t xy_msg;
    memset(&xy_msg, 0, sizeof(xy_msg));
    xy_msg.report_id = HIDPP_REPORT_LONG;
    xy_msg.device_index = TEST_DEVICE_INDEX;
    xy_msg.feature_index = MOCK_FEATURE_IDX_REPROG;
    xy_msg.function_id = 0x01;
    xy_msg.software_id = 0x00;
    xy_msg.payload_len = 4;
    /* dx = 120 (0x0078), dy = -45 (0xFFD3) */
    xy_msg.payload[0] = 0x00; xy_msg.payload[1] = 0x78;
    xy_msg.payload[2] = 0xFF; xy_msg.payload[3] = 0xD3;

    ok = hidpp_reprog_decode_event(&xy_msg, MOCK_FEATURE_IDX_REPROG, &event);
    assert(ok == true);
    assert(event.type == REPROG_EVENT_RAW_XY);
    assert(event.u.raw_xy.dx == 120);
    assert(event.u.raw_xy.dy == -45);
}

static void test_diversion_safety_registry_and_teardown(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    assert(dev != NULL);

    /* Divert two controls */
    hidpp_reprog_set_reporting(
        dev, TEST_DEVICE_INDEX, MOCK_FEATURE_IDX_REPROG, TEST_SW_ID, HIDPP_CID_GESTURE_BUTTON, true, true
    );
    hidpp_reprog_set_reporting(
        dev, TEST_DEVICE_INDEX, MOCK_FEATURE_IDX_REPROG, TEST_SW_ID, HIDPP_CID_BACK_CLASSIC, true, false
    );

    /* Call emergency teardown: simulates SIGINT or process termination */
    hidpp_diversion_teardown_all();

    /* Second call should be a no-op */
    hidpp_diversion_teardown_all();

    hid_mock_destroy(dev);
}

static hook_action_t dummy_hook_cb(const hook_event_t *ev, void *user_data)
{
    (void)ev;
    (void)user_data;
    return HOOK_ACTION_PASS;
}

static void test_hook_lifecycle(void)
{
    bool ok = hook_init(dummy_hook_cb, NULL);
    assert(ok == true);
    assert(hook_is_running() == false);

    /* Permission check should not crash or hang */
    (void)hook_check_permission();
}

static hook_action_t invert_hook_cb(const hook_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev && ev->type == HOOK_EVENT_SCROLL) {
        return HOOK_ACTION_INVERT;
    }
    return HOOK_ACTION_PASS;
}

static void test_hook_scroll_actions(void)
{
    hook_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = HOOK_EVENT_SCROLL;
    ev.scroll_dy = -1.0;

    assert(invert_hook_cb(&ev, NULL) == HOOK_ACTION_INVERT);
    assert(dummy_hook_cb(&ev, NULL) == HOOK_ACTION_PASS);
}

int main(void)
{
    test_suite_begin(&g_suite, "test_diversion_and_hook", "ReprogControls 0x1B04 Diversion & Event Hooks");

    TEST_CASE_RUN(g_suite, test_reprog_get_count);
    TEST_CASE_RUN(g_suite, test_reprog_get_cid_info);
    TEST_CASE_RUN(g_suite, test_reprog_set_and_get_reporting);
    TEST_CASE_RUN(g_suite, test_reprog_event_decoding);
    TEST_CASE_RUN(g_suite, test_diversion_safety_registry_and_teardown);
    TEST_CASE_RUN(g_suite, test_hook_lifecycle);
    TEST_CASE_RUN(g_suite, test_hook_scroll_actions);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
