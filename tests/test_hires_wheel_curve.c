/**
 * @file test_hires_wheel_curve.c
 * @brief Regression tests for HID++ 0x2121 hi-res wheel and 0x2110 SmartShift write payloads.
 */

#include "hires_wheel.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void test_hires_wheel_mode_payload(void)
{
    hidpp_v20_msg_t msg;

    /* 1. Normal hi-res reporting (not inverted) */
    assert(hires_wheel_build_mode_request(0x05, 0x02, false, true, &msg));
    assert(msg.feature_index == 0x05);
    assert(msg.function_id == 0x01);
    assert(msg.payload[0] == HIRES_FLAG_REPORTING); /* 0x02 */
    assert(msg.payload_len == 1);

    /* 2. Inverted hi-res reporting */
    assert(hires_wheel_build_mode_request(0x05, 0x02, true, true, &msg));
    assert(msg.payload[0] == (HIRES_FLAG_INVERTED | HIRES_FLAG_REPORTING)); /* 0x03 */

    /* 3. Standard low-res (no hi-res reporting bit) */
    assert(hires_wheel_build_mode_request(0x05, 0x02, false, false, &msg));
    assert(msg.payload[0] == 0x00);
}

static void test_hires_wheel_multiplier_ratchet_curve(void)
{
    /* MX Master 3S: 120 hi-res counts per revolution, 24 ratchets -> multiplier = 5 */
    assert(hires_wheel_calculate_multiplier(120, 24) == 5);

    /* MX Anywhere 3: 144 counts per revolution, 24 ratchets -> multiplier = 6 */
    assert(hires_wheel_calculate_multiplier(144, 24) == 6);

    /* Fallback on 0 */
    assert(hires_wheel_calculate_multiplier(120, 0) == 1);
}

static void test_smartshift_curve_threshold_bounds(void)
{
    /* Valid threshold */
    assert(smartshift_clamp_threshold(16) == 16);

    /* Lower bound (minimum sensitivity) */
    assert(smartshift_clamp_threshold(0) == 1);
    assert(smartshift_clamp_threshold(-5) == 1);

    /* Upper bound (maximum sensitivity) */
    assert(smartshift_clamp_threshold(255) == 255);
    assert(smartshift_clamp_threshold(300) == 255);
}

static void test_smartshift_auto_disengage_mode_payload(void)
{
    hidpp_v20_msg_t msg;

    /* Ratchet mode with threshold 16 */
    assert(smartshift_build_set_request(0x04, 0x03, SMARTSHIFT_MODE_RATCHET, 16, &msg));
    assert(msg.feature_index == 0x04);
    assert(msg.function_id == 0x01);
    assert(msg.payload[0] == SMARTSHIFT_MODE_RATCHET);
    assert(msg.payload[1] == 16);
    assert(msg.payload_len == 2);

    /* FreeSpin mode with threshold 0 */
    assert(smartshift_build_set_request(0x04, 0x03, SMARTSHIFT_MODE_FREESPIN, 0, &msg));
    assert(msg.payload[0] == SMARTSHIFT_MODE_FREESPIN);
    assert(msg.payload[1] == 0);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_hires_wheel_curve", "HID++ 0x2121 Hi-Res Wheel & 0x2110 SmartShift Curves");

    TEST_CASE_RUN(g_suite, test_hires_wheel_mode_payload);
    TEST_CASE_RUN(g_suite, test_hires_wheel_multiplier_ratchet_curve);
    TEST_CASE_RUN(g_suite, test_smartshift_curve_threshold_bounds);
    TEST_CASE_RUN(g_suite, test_smartshift_auto_disengage_mode_payload);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
