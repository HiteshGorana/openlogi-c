/**
 * @file test_device_probe.c
 * @brief Unit tests for Milestone 2: Device feature probing, battery, DPI, and SmartShift.
 */

#include "device.h"
#include "hidpp.h"
#include "hid_transport.h"
#include "hid_mock.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

TEST_SUITE_DECLARE(g_suite);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

static void test_mock_device_creation(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    TEST_ASSERT(dev != NULL, "Mock device creation failed");

    const hid_device_info_t *info = hid_device_get_info(dev);
    TEST_ASSERT(info != NULL, "Failed to get info");
    TEST_ASSERT(info->vendor_id == LOGITECH_VENDOR_ID, "Vendor ID must be 0x046D");
    TEST_ASSERT(strcmp(info->product_name, "MX Master 3S") == 0, "Name mismatch");
    TEST_ASSERT(strcmp(info->serial_number, "6be9d300") == 0, "Serial mismatch");

    hid_mock_destroy(dev);
}

static void test_probe_device_features(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    TEST_ASSERT(dev != NULL, "Mock device creation failed");

    openlogi_device_t probed;
    bool ok = openlogi_probe_device(dev, 0xFF, HIDPP_DEFAULT_SW_ID, &probed);
    TEST_ASSERT(ok, "Device probe must succeed");

    /* Protocol & Identity */
    TEST_ASSERT(probed.proto_version == HIDPP_PROTO_V20, "Must be HID++ 2.0");
    TEST_ASSERT(strcmp(probed.name, "MX Master 3S") == 0, "Probed name must be MX Master 3S");
    TEST_ASSERT(probed.vendor_id == LOGITECH_VENDOR_ID, "Vendor ID mismatch");

    /* Feature indices */
    TEST_ASSERT(probed.feat_name == 0x02, "Feature Name index must be 2");
    TEST_ASSERT(probed.feat_battery == 0x03, "Feature Battery index must be 3");
    TEST_ASSERT(probed.is_unified_battery == true, "Must be unified battery 0x1004");
    TEST_ASSERT(probed.feat_dpi == 0x04, "Feature DPI index must be 4");
    TEST_ASSERT(probed.feat_smartshift == 0x05, "Feature SmartShift index must be 5");
    TEST_ASSERT(probed.feat_reprog_controls == 0x06, "Feature ReprogControls index must be 6");

    /* Values probed from hardware */
    TEST_ASSERT(probed.battery_percent == 85, "Battery must be 85%");
    TEST_ASSERT(probed.battery_charging == false, "Must be discharging");
    TEST_ASSERT(probed.current_dpi == 1200, "Initial DPI must be 1200");
    TEST_ASSERT(probed.min_dpi == 200, "Min DPI must be 200");
    TEST_ASSERT(probed.max_dpi == 4000, "Max DPI must be 4000");
    TEST_ASSERT(probed.dpi_step == 50, "DPI step must be 50");
    TEST_ASSERT(probed.smartshift_mode == SMARTSHIFT_MODE_AUTO, "SmartShift mode must be Auto");
    TEST_ASSERT(probed.smartshift_threshold == 14, "SmartShift threshold must be 14");

    hid_mock_destroy(dev);
}

static void test_dpi_read_and_write(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    TEST_ASSERT(dev != NULL, "Mock device creation failed");

    openlogi_device_t probed;
    bool ok = openlogi_probe_device(dev, 0xFF, HIDPP_DEFAULT_SW_ID, &probed);
    TEST_ASSERT(ok, "Probe failed");
    TEST_ASSERT(probed.current_dpi == 1200, "Initial DPI should be 1200");

    /* Write new DPI (2400) */
    ok = hidpp_write_dpi(dev, 0xFF, probed.feat_dpi, HIDPP_DEFAULT_SW_ID, 2400);
    TEST_ASSERT(ok, "Writing DPI 2400 should succeed");

    /* Read DPI back */
    uint16_t new_dpi = 0;
    ok = hidpp_read_dpi(dev, 0xFF, probed.feat_dpi, HIDPP_DEFAULT_SW_ID, &new_dpi, NULL, NULL, NULL);
    TEST_ASSERT(ok, "Reading DPI back should succeed");
    TEST_ASSERT(new_dpi == 2400, "Read DPI must reflect new value 2400");
    TEST_ASSERT(hid_mock_get_dpi(dev) == 2400, "Mock internal state must be updated");

    hid_mock_destroy(dev);
}

static void test_smartshift_read_and_write(void)
{
    hid_device_t *dev = hid_mock_create_mx_master_3s();
    TEST_ASSERT(dev != NULL, "Mock device creation failed");

    openlogi_device_t probed;
    bool ok = openlogi_probe_device(dev, 0xFF, HIDPP_DEFAULT_SW_ID, &probed);
    TEST_ASSERT(ok, "Probe failed");

    /* Set mode to Ratchet with threshold 20 */
    ok = hidpp_write_smartshift(dev, 0xFF, probed.feat_smartshift, HIDPP_DEFAULT_SW_ID,
                                SMARTSHIFT_MODE_RATCHET, 20);
    TEST_ASSERT(ok, "Writing SmartShift should succeed");

    /* Read back */
    smartshift_mode_t mode = SMARTSHIFT_MODE_DISABLED;
    uint8_t threshold = 0;
    ok = hidpp_read_smartshift(dev, 0xFF, probed.feat_smartshift, HIDPP_DEFAULT_SW_ID,
                               &mode, &threshold);
    TEST_ASSERT(ok, "Reading SmartShift back should succeed");
    TEST_ASSERT(mode == SMARTSHIFT_MODE_RATCHET, "Mode must be Ratchet (2)");
    TEST_ASSERT(threshold == 20, "Threshold must be 20");

    uint8_t mock_mode = 0, mock_thresh = 0;
    hid_mock_get_smartshift(dev, &mock_mode, &mock_thresh);
    TEST_ASSERT(mock_mode == SMARTSHIFT_MODE_RATCHET, "Mock mode must match");
    TEST_ASSERT(mock_thresh == 20, "Mock threshold must match");

    hid_mock_destroy(dev);
}

static void test_native_enumeration_lifecycle(void)
{
    int init_res = hid_transport_init();
    /* On macOS without Input Monitoring granted, init may return -2, which is handled gracefully */
    if (init_res == 0) {
        hid_device_info_t list[8];
        int count = hid_enumerate(list, 8);
        TEST_ASSERT(count >= 0, "Enumeration must return non-negative count");
        printf("(discovered %d live Logitech device(s)) ", count);
        hid_transport_shutdown();
    } else {
        printf("(IOKit init returned %d, likely permission gate, handled gracefully) ", init_res);
    }
}

/* =========================================================================
 * Main Runner
 * ========================================================================= */

int main(void)
{
    test_suite_begin(&g_suite, "test_device_probe", "Hardware Probing, Battery, DPI & SmartShift");

    TEST_CASE_RUN(g_suite, test_mock_device_creation);
    TEST_CASE_RUN(g_suite, test_probe_device_features);
    TEST_CASE_RUN(g_suite, test_dpi_read_and_write);
    TEST_CASE_RUN(g_suite, test_smartshift_read_and_write);
    TEST_CASE_RUN(g_suite, test_native_enumeration_lifecycle);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
