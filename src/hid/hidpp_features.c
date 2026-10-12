/**
 * @file hidpp_features.c
 * @brief Implementation of Logitech HID++ feature queries and device probing.
 */

#include "device.h"
#include "hidpp.h"
#include "hid_transport.h"

#include <string.h>
#include <stdio.h>

#define TRANSACTION_TIMEOUT_MS 1500

/* Helper to lookup a feature index via Root feature (0x0000, func 0) */
static uint8_t query_feature_index(
    hid_device_t *dev,
    uint8_t device_index,
    uint16_t feature_id,
    uint8_t sw_id)
{
    hidpp_v20_msg_t req;
    hidpp_make_get_feature(device_index, feature_id, sw_id, &req);

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS)) {
        return 0;
    }

    if (resp.proto != HIDPP_PROTO_V20) {
        return 0;
    }

    uint8_t feature_idx = 0;
    if (!hidpp_parse_get_feature_response(&resp.u.v20, &feature_idx, NULL)) {
        return 0;
    }

    return feature_idx;
}

bool openlogi_probe_device(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t sw_id,
    openlogi_device_t *out_device)
{
    if (!dev || !out_device) {
        return false;
    }

    memset(out_device, 0, sizeof(*out_device));
    out_device->device_index = device_index;
    out_device->sw_id = sw_id;

    const hid_device_info_t *info = hid_device_get_info(dev);
    if (info) {
        out_device->vendor_id = info->vendor_id;
        out_device->product_id = info->product_id;
        if (info->product_name[0] != '\0') {
            strncpy(out_device->name, info->product_name, sizeof(out_device->name) - 1);
        }
        if (info->serial_number[0] != '\0') {
            snprintf(out_device->id, sizeof(out_device->id), "unit:%s", info->serial_number);
        } else {
            snprintf(out_device->id, sizeof(out_device->id), "direct:%04x:%04x:idx:%d",
                     info->vendor_id, info->product_id, device_index);
        }
    }

    /* Step 1: Ping device to determine protocol version (HID++ 1.0 vs 2.0+) */
    hidpp_v20_msg_t ping_req;
    hidpp_make_ping(device_index, sw_id, &ping_req);

    hidpp_msg_t ping_resp;
    if (!hid_transact(dev, &ping_req, &ping_resp, TRANSACTION_TIMEOUT_MS)) {
        return false; /* Device did not answer ping */
    }

    hidpp_version_info_t ver_info;
    if (!hidpp_parse_ping_response(&ping_req, &ping_resp, &ver_info)) {
        return false;
    }

    out_device->proto_version = ver_info.version;
    if (ver_info.version != HIDPP_PROTO_V20) {
        /* Device is HID++ 1.0 (older hardware or receiver) */
        return true;
    }

    /* Step 2: Feature Discovery for HID++ 2.0+ */
    out_device->feat_root = HIDPP_FEATURE_ROOT;
    out_device->feat_feature_set = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_FEATURE_SET, sw_id);
    out_device->feat_name = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_DEVICE_NAME, sw_id);

    /* Battery: Try Unified (0x1004) first, then fallback to Legacy (0x1000) */
    uint8_t bat_idx = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_UNIFIED_BATTERY, sw_id);
    if (bat_idx != 0) {
        out_device->feat_battery = bat_idx;
        out_device->is_unified_battery = true;
    } else {
        bat_idx = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_BATTERY_STATUS, sw_id);
        if (bat_idx != 0) {
            out_device->feat_battery = bat_idx;
            out_device->is_unified_battery = false;
        }
    }

    /* DPI: Feature 0x2201 or 0x2205 */
    out_device->feat_dpi = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_ADJUSTABLE_DPI, sw_id);
    if (out_device->feat_dpi == 0) {
        out_device->feat_dpi = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_EXTENDED_DPI, sw_id);
    }

    /* SmartShift: Feature 0x2110 or 0x2111 */
    out_device->feat_smartshift = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_SMARTSHIFT, sw_id);
    if (out_device->feat_smartshift == 0) {
        out_device->feat_smartshift = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_SMARTSHIFT_ENHANCED, sw_id);
    }

    /* ReprogControls & Wheels */
    out_device->feat_reprog_controls = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_REPROG_CONTROLS_V4, sw_id);
    out_device->feat_hires_wheel = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_HIRES_WHEEL, sw_id);
    out_device->feat_thumbwheel = query_feature_index(dev, device_index, HIDPP_FEATURE_ID_THUMBWHEEL, sw_id);

    /* Step 3: Read Device Marketing Name if feature present */
    if (out_device->feat_name != 0) {
        char fw_name[128];
        if (hidpp_read_device_name(dev, device_index, out_device->feat_name, sw_id, fw_name, sizeof(fw_name))) {
            if (fw_name[0] != '\0') {
                strncpy(out_device->name, fw_name, sizeof(out_device->name) - 1);
            }
        }
    }

    /* Step 4: Read Battery State */
    if (out_device->feat_battery != 0) {
        hidpp_read_battery(dev, device_index, out_device->feat_battery,
                           out_device->is_unified_battery, sw_id,
                           &out_device->battery_percent, &out_device->battery_charging);
    }

    /* Step 5: Read DPI Settings */
    if (out_device->feat_dpi != 0) {
        hidpp_read_dpi(dev, device_index, out_device->feat_dpi, sw_id,
                       &out_device->current_dpi, &out_device->min_dpi,
                       &out_device->max_dpi, &out_device->dpi_step);
    }

    /* Step 6: Read SmartShift Settings */
    if (out_device->feat_smartshift != 0) {
        hidpp_read_smartshift(dev, device_index, out_device->feat_smartshift, sw_id,
                              &out_device->smartshift_mode, &out_device->smartshift_threshold);
    }

    return true;
}

bool hidpp_read_device_name(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_name_idx,
    uint8_t sw_id,
    char *out_name,
    size_t max_len)
{
    if (!dev || !out_name || max_len == 0 || feat_name_idx == 0) {
        return false;
    }

    out_name[0] = '\0';

    /* Function 0: getDeviceNameCount */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_name_idx;
    req.function_id = 0x00; /* getDeviceNameCount */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) || resp.proto != HIDPP_PROTO_V20) {
        return false;
    }

    uint8_t total_chars = resp.u.v20.payload[0];
    if (total_chars == 0) {
        return false;
    }

    /* Function 1: getDeviceName(index) */
    size_t received = 0;
    while (received < total_chars && received + 1 < max_len) {
        memset(&req, 0, sizeof(req));
        req.report_id = HIDPP_REPORT_SHORT;
        req.device_index = device_index;
        req.feature_index = feat_name_idx;
        req.function_id = 0x01; /* getDeviceName */
        req.software_id = sw_id & 0x0F;
        req.payload_len = 3;
        req.payload[0] = (uint8_t)received;

        if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) || resp.proto != HIDPP_PROTO_V20) {
            break;
        }

        size_t chunk_len = resp.u.v20.payload_len;
        if (chunk_len == 0) {
            break;
        }

        for (size_t i = 0; i < chunk_len && received + 1 < max_len && received < total_chars; i++) {
            out_name[received++] = (char)resp.u.v20.payload[i];
        }
    }

    out_name[received] = '\0';
    return (received > 0);
}

bool hidpp_read_battery(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_battery_idx,
    bool is_unified,
    uint8_t sw_id,
    uint8_t *out_percent,
    bool *out_charging)
{
    if (!dev || feat_battery_idx == 0) {
        return false;
    }

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_battery_idx;
    req.function_id = is_unified ? 0x01 : 0x00; /* 0x1004 func 1, 0x1000 func 0 */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) || resp.proto != HIDPP_PROTO_V20) {
        return false;
    }

    if (resp.u.v20.payload_len < 3) {
        return false;
    }

    if (out_percent) {
        *out_percent = resp.u.v20.payload[0]; /* discharge level or charge percentage */
    }
    if (out_charging) {
        uint8_t status = resp.u.v20.payload[2];
        *out_charging = (status == 1 || status == 2 || status == 4); /* Recharging statuses */
    }

    return true;
}

bool hidpp_read_dpi(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_dpi_idx,
    uint8_t sw_id,
    uint16_t *out_current_dpi,
    uint16_t *out_min_dpi,
    uint16_t *out_max_dpi,
    uint16_t *out_dpi_step)
{
    if (!dev || feat_dpi_idx == 0) {
        return false;
    }

    /* Function 2: getSensorDpi(sensor_index = 0) */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_dpi_idx;
    req.function_id = 0x02; /* getSensorDpi */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;
    req.payload[0] = 0x00; /* Sensor 0 */

    hidpp_msg_t resp;
    if (hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) && resp.proto == HIDPP_PROTO_V20) {
        if (resp.u.v20.payload_len >= 3 && out_current_dpi) {
            *out_current_dpi = (uint16_t)((resp.u.v20.payload[1] << 8) | resp.u.v20.payload[2]);
        }
    }

    /* Function 1: getSensorDpiList(sensor_index = 0) */
    req.function_id = 0x01;
    if (hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) && resp.proto == HIDPP_PROTO_V20) {
        /* Parse first range marker or explicit values */
        uint16_t min_dpi = 200;
        uint16_t max_dpi = 4000;
        uint16_t step = 50;

        if (resp.u.v20.payload_len >= 5) {
            min_dpi = (uint16_t)((resp.u.v20.payload[1] << 8) | resp.u.v20.payload[2]);
            uint16_t marker = (uint16_t)((resp.u.v20.payload[3] << 8) | resp.u.v20.payload[4]);
            if ((marker & 0xE000) == 0xE000) {
                step = marker & 0x1FFF;
                if (resp.u.v20.payload_len >= 7) {
                    max_dpi = (uint16_t)((resp.u.v20.payload[5] << 8) | resp.u.v20.payload[6]);
                }
            }
        }

        if (out_min_dpi) *out_min_dpi = min_dpi;
        if (out_max_dpi) *out_max_dpi = max_dpi;
        if (out_dpi_step) *out_dpi_step = step;
    }

    return true;
}

bool hidpp_write_dpi(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_dpi_idx,
    uint8_t sw_id,
    uint16_t dpi)
{
    if (!dev || feat_dpi_idx == 0) {
        return false;
    }

    /* Function 3: setSensorDpi(sensor_index, dpi_hi, dpi_lo) */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_dpi_idx;
    req.function_id = 0x03; /* setSensorDpi */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;
    req.payload[0] = 0x00; /* Sensor 0 */
    req.payload[1] = (uint8_t)((dpi >> 8) & 0xFF);
    req.payload[2] = (uint8_t)(dpi & 0xFF);

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS)) {
        return false;
    }

    return (resp.proto == HIDPP_PROTO_V20);
}

bool hidpp_read_smartshift(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_smartshift_idx,
    uint8_t sw_id,
    smartshift_mode_t *out_mode,
    uint8_t *out_threshold)
{
    if (!dev || feat_smartshift_idx == 0) {
        return false;
    }

    /* Function 0: getSmartShiftStatus */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_smartshift_idx;
    req.function_id = 0x00; /* getRatchetControlMode */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS) || resp.proto != HIDPP_PROTO_V20) {
        return false;
    }

    if (resp.u.v20.payload_len >= 2) {
        if (out_mode) {
            *out_mode = (smartshift_mode_t)resp.u.v20.payload[0];
        }
        if (out_threshold) {
            *out_threshold = resp.u.v20.payload[1];
        }
        return true;
    }

    return false;
}

bool hidpp_write_smartshift(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_smartshift_idx,
    uint8_t sw_id,
    smartshift_mode_t mode,
    uint8_t threshold)
{
    if (!dev || feat_smartshift_idx == 0) {
        return false;
    }

    /* Function 1: setSmartShiftStatus */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feat_smartshift_idx;
    req.function_id = 0x01; /* setRatchetControlMode */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;
    req.payload[0] = (uint8_t)mode;
    req.payload[1] = threshold;
    req.payload[2] = 0x00; /* Keep default threshold unchanged */

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, TRANSACTION_TIMEOUT_MS)) {
        return false;
    }

    return (resp.proto == HIDPP_PROTO_V20);
}
