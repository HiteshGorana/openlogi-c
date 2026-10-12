/**
 * @file hires_wheel.c
 * @brief Implementation of HID++ 2.0 Feature 0x2121 and 0x2110 payloads.
 */

#include "hires_wheel.h"

#include <string.h>

bool hires_wheel_build_mode_request(uint8_t feature_idx, uint8_t sw_id,
                                    bool inverted, bool hires_reporting,
                                    hidpp_v20_msg_t *out_msg)
{
    if (!out_msg) return false;
    memset(out_msg, 0, sizeof(*out_msg));

    out_msg->report_id = HIDPP_REPORT_SHORT;
    out_msg->device_index = 0xFF;
    out_msg->feature_index = feature_idx;
    out_msg->function_id = 0x01; /* Set Mode */
    out_msg->software_id = sw_id;

    uint8_t flags = 0;
    if (inverted) flags |= HIRES_FLAG_INVERTED;
    if (hires_reporting) flags |= HIRES_FLAG_REPORTING;

    out_msg->payload[0] = flags;
    out_msg->payload_len = 1;
    return true;
}

bool smartshift_build_set_request(uint8_t feature_idx, uint8_t sw_id,
                                  uint8_t mode, uint8_t threshold,
                                  hidpp_v20_msg_t *out_msg)
{
    if (!out_msg) return false;
    memset(out_msg, 0, sizeof(*out_msg));

    out_msg->report_id = HIDPP_REPORT_SHORT;
    out_msg->device_index = 0xFF;
    out_msg->feature_index = feature_idx;
    out_msg->function_id = 0x01; /* Set SmartShift */
    out_msg->software_id = sw_id;

    out_msg->payload[0] = mode;
    out_msg->payload[1] = threshold;
    out_msg->payload_len = 2;
    return true;
}

uint16_t hires_wheel_calculate_multiplier(uint16_t hires_ticks, uint16_t native_notches)
{
    if (native_notches == 0) return 1;
    return hires_ticks / native_notches;
}

uint8_t smartshift_clamp_threshold(int threshold)
{
    if (threshold < 1) return 1;
    if (threshold > 255) return 255;
    return (uint8_t)threshold;
}
