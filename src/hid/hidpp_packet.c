/**
 * @file hidpp_packet.c
 * @brief Implementation of Logitech HID++ 1.0 and 2.0 packet framing.
 */

#include "hidpp.h"

#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Packet Packing & Unpacking (HID++ 2.0)
 * ========================================================================= */

bool hidpp_v20_pack(const hidpp_v20_msg_t *msg, hidpp_raw_report_t *raw)
{
    if (!msg || !raw) {
        return false;
    }

    size_t target_len;
    uint8_t report_id;

    if (msg->payload_len <= HIDPP_V20_SHORT_PAYLOAD_LEN) {
        report_id = HIDPP_REPORT_SHORT;
        target_len = HIDPP_SHORT_LEN;
    } else if (msg->payload_len <= HIDPP_V20_LONG_PAYLOAD_LEN) {
        report_id = HIDPP_REPORT_LONG;
        target_len = HIDPP_LONG_LEN;
    } else if (msg->payload_len <= HIDPP_V20_MAX_PAYLOAD_LEN) {
        report_id = HIDPP_REPORT_VERY_LONG;
        target_len = HIDPP_VERY_LONG_LEN;
    } else {
        return false; /* Payload exceeds maximum supported 60 bytes */
    }

    /* Override with caller's report_id if caller explicitly set a valid larger one */
    if (msg->report_id == HIDPP_REPORT_LONG && target_len < HIDPP_LONG_LEN) {
        report_id = HIDPP_REPORT_LONG;
        target_len = HIDPP_LONG_LEN;
    } else if (msg->report_id == HIDPP_REPORT_VERY_LONG && target_len < HIDPP_VERY_LONG_LEN) {
        report_id = HIDPP_REPORT_VERY_LONG;
        target_len = HIDPP_VERY_LONG_LEN;
    }

    memset(raw->data, 0, target_len);
    raw->data[0] = report_id;
    raw->data[1] = msg->device_index;
    raw->data[2] = msg->feature_index;
    raw->data[3] = (uint8_t)(((msg->function_id & 0x0F) << 4) | (msg->software_id & 0x0F));

    if (msg->payload_len > 0) {
        memcpy(&raw->data[4], msg->payload, msg->payload_len);
    }

    raw->length = target_len;
    return true;
}

bool hidpp_v20_unpack(const hidpp_raw_report_t *raw, hidpp_v20_msg_t *msg)
{
    if (!raw || !msg || raw->length < 4) {
        return false;
    }

    uint8_t report_id = raw->data[0];
    size_t max_payload;

    if (report_id == HIDPP_REPORT_SHORT) {
        if (raw->length < HIDPP_SHORT_LEN) {
            return false;
        }
        max_payload = HIDPP_V20_SHORT_PAYLOAD_LEN;
    } else if (report_id == HIDPP_REPORT_LONG) {
        if (raw->length < HIDPP_LONG_LEN) {
            return false;
        }
        max_payload = HIDPP_V20_LONG_PAYLOAD_LEN;
    } else if (report_id == HIDPP_REPORT_VERY_LONG) {
        if (raw->length < HIDPP_VERY_LONG_LEN) {
            return false;
        }
        max_payload = HIDPP_V20_MAX_PAYLOAD_LEN;
    } else {
        return false; /* Unknown report ID */
    }

    msg->report_id = report_id;
    msg->device_index = raw->data[1];
    msg->feature_index = raw->data[2];
    msg->function_id = (raw->data[3] >> 4) & 0x0F;
    msg->software_id = raw->data[3] & 0x0F;

    size_t available_payload = raw->length - 4;
    msg->payload_len = available_payload < max_payload ? available_payload : max_payload;

    memset(msg->payload, 0, sizeof(msg->payload));
    if (msg->payload_len > 0) {
        memcpy(msg->payload, &raw->data[4], msg->payload_len);
    }

    return true;
}

/* =========================================================================
 * Packet Packing & Unpacking (HID++ 1.0)
 * ========================================================================= */

bool hidpp_v10_pack(const hidpp_v10_msg_t *msg, hidpp_raw_report_t *raw)
{
    if (!msg || !raw) {
        return false;
    }

    size_t target_len;
    uint8_t report_id;

    if (msg->payload_len <= HIDPP_V10_SHORT_PAYLOAD_LEN) {
        report_id = HIDPP_REPORT_SHORT;
        target_len = HIDPP_SHORT_LEN;
    } else if (msg->payload_len <= HIDPP_V10_LONG_PAYLOAD_LEN) {
        report_id = HIDPP_REPORT_LONG;
        target_len = HIDPP_LONG_LEN;
    } else {
        return false;
    }

    if (msg->report_id == HIDPP_REPORT_LONG && target_len < HIDPP_LONG_LEN) {
        report_id = HIDPP_REPORT_LONG;
        target_len = HIDPP_LONG_LEN;
    }

    memset(raw->data, 0, target_len);
    raw->data[0] = report_id;
    raw->data[1] = msg->device_index;
    raw->data[2] = msg->sub_id;

    if (msg->payload_len > 0) {
        memcpy(&raw->data[3], msg->payload, msg->payload_len);
    }

    raw->length = target_len;
    return true;
}

bool hidpp_v10_unpack(const hidpp_raw_report_t *raw, hidpp_v10_msg_t *msg)
{
    if (!raw || !msg || raw->length < 3) {
        return false;
    }

    uint8_t report_id = raw->data[0];
    size_t max_payload;

    if (report_id == HIDPP_REPORT_SHORT) {
        if (raw->length < HIDPP_SHORT_LEN) {
            return false;
        }
        max_payload = HIDPP_V10_SHORT_PAYLOAD_LEN;
    } else if (report_id == HIDPP_REPORT_LONG) {
        if (raw->length < HIDPP_LONG_LEN) {
            return false;
        }
        max_payload = HIDPP_V10_LONG_PAYLOAD_LEN;
    } else {
        return false;
    }

    msg->report_id = report_id;
    msg->device_index = raw->data[1];
    msg->sub_id = raw->data[2];

    size_t available_payload = raw->length - 3;
    msg->payload_len = available_payload < max_payload ? available_payload : max_payload;

    memset(msg->payload, 0, sizeof(msg->payload));
    if (msg->payload_len > 0) {
        memcpy(msg->payload, &raw->data[3], msg->payload_len);
    }

    return true;
}

bool hidpp_unpack(const hidpp_raw_report_t *raw, hidpp_msg_t *out_msg)
{
    if (!raw || !out_msg || raw->length < 4) {
        return false;
    }

    /* Check for HID++ 1.0 Error report first (Sub ID 0x8F) */
    if (raw->data[2] == HIDPP_V10_SUB_ID_ERROR) {
        if (hidpp_v10_unpack(raw, &out_msg->u.v10)) {
            out_msg->proto = HIDPP_PROTO_V10;
            return true;
        }
    }

    /* Default to HID++ 2.0 unpacking */
    if (hidpp_v20_unpack(raw, &out_msg->u.v20)) {
        out_msg->proto = HIDPP_PROTO_V20;
        return true;
    }

    /* Fallback to HID++ 1.0 unpacking */
    if (hidpp_v10_unpack(raw, &out_msg->u.v10)) {
        out_msg->proto = HIDPP_PROTO_V10;
        return true;
    }

    return false;
}

/* =========================================================================
 * Error Detection & Matching
 * ========================================================================= */

bool hidpp_v20_is_error(
    const hidpp_v20_msg_t *msg,
    uint8_t *out_feature_index,
    uint8_t *out_func_id,
    uint8_t *out_sw_id,
    hidpp20_error_t *out_err)
{
    if (!msg || msg->feature_index != HIDPP_FEATURE_ERROR) {
        return false;
    }

    if (msg->payload_len < 3) {
        return false;
    }

    if (out_feature_index) {
        *out_feature_index = msg->payload[0];
    }
    if (out_func_id) {
        *out_func_id = (msg->payload[1] >> 4) & 0x0F;
    }
    if (out_sw_id) {
        *out_sw_id = msg->payload[1] & 0x0F;
    }
    if (out_err) {
        *out_err = (hidpp20_error_t)msg->payload[2];
    }

    return true;
}

bool hidpp_v10_is_error(
    const hidpp_v10_msg_t *msg,
    uint8_t *out_sub_id,
    hidpp10_error_t *out_err)
{
    if (!msg || msg->sub_id != HIDPP_V10_SUB_ID_ERROR) {
        return false;
    }

    if (msg->payload_len < 3) {
        return false;
    }

    if (out_sub_id) {
        *out_sub_id = msg->payload[0];
    }
    if (out_err) {
        *out_err = (hidpp10_error_t)msg->payload[2];
    }

    return true;
}

bool hidpp_v20_matches_request(const hidpp_v20_msg_t *req, const hidpp_v20_msg_t *resp)
{
    if (!req || !resp) {
        return false;
    }

    /* Device index must match */
    if (req->device_index != resp->device_index) {
        return false;
    }

    /* Case A: Normal response (Feature index matches) */
    if (req->feature_index == resp->feature_index) {
        return (req->function_id == resp->function_id) &&
               (req->software_id == resp->software_id);
    }

    /* Case B: HID++ 2.0 Error response (Feature index is 0xFF) */
    if (resp->feature_index == HIDPP_FEATURE_ERROR) {
        uint8_t err_feature = 0;
        uint8_t err_func = 0;
        uint8_t err_sw = 0;

        if (hidpp_v20_is_error(resp, &err_feature, &err_func, &err_sw, NULL)) {
            return (req->feature_index == err_feature) &&
                   (req->function_id == err_func) &&
                   (req->software_id == err_sw);
        }
    }

    return false;
}

/* =========================================================================
 * Command Builders & Parsers
 * ========================================================================= */

void hidpp_make_ping(uint8_t device_index, uint8_t sw_id, hidpp_v20_msg_t *out_msg)
{
    if (!out_msg) {
        return;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->report_id = HIDPP_REPORT_SHORT;
    out_msg->device_index = device_index;
    out_msg->feature_index = HIDPP_FEATURE_ROOT;
    out_msg->function_id = 0x01; /* Ping function */
    out_msg->software_id = sw_id & 0x0F;
    out_msg->payload_len = 3;
    out_msg->payload[0] = 0x00;
    out_msg->payload[1] = 0x00;
    out_msg->payload[2] = 0x00;
}

bool hidpp_parse_ping_response(
    const hidpp_v20_msg_t *req,
    const hidpp_msg_t *resp,
    hidpp_version_info_t *out_info)
{
    if (!req || !resp || !out_info) {
        return false;
    }

    memset(out_info, 0, sizeof(*out_info));

    /* Case 1: Device answered with HID++ 2.0 response */
    if (resp->proto == HIDPP_PROTO_V20) {
        if (hidpp_v20_matches_request(req, &resp->u.v20)) {
            if (resp->u.v20.payload_len >= 2) {
                out_info->version = HIDPP_PROTO_V20;
                out_info->protocol_num = resp->u.v20.payload[0];
                out_info->target_sw = resp->u.v20.payload[1];
                return true;
            }
        }
    }

    /* Case 2: Device answered with HID++ 1.0 error (0x8F Invalid SubID) */
    if (resp->proto == HIDPP_PROTO_V10) {
        const hidpp_v10_msg_t *v10 = &resp->u.v10;
        if (v10->device_index == req->device_index &&
            v10->sub_id == HIDPP_V10_SUB_ID_ERROR &&
            v10->payload_len >= 3) {
            /* payload[0] was the feature index (0x00) we sent, payload[2] is error code */
            if (v10->payload[0] == 0x00 && v10->payload[2] == HIDPP10_ERR_INVALID_SUBID) {
                out_info->version = HIDPP_PROTO_V10;
                out_info->protocol_num = 1;
                out_info->target_sw = 0;
                return true;
            }
        }
    }

    return false;
}

void hidpp_make_get_feature(
    uint8_t device_index,
    uint16_t feature_id,
    uint8_t sw_id,
    hidpp_v20_msg_t *out_msg)
{
    if (!out_msg) {
        return;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->report_id = HIDPP_REPORT_SHORT;
    out_msg->device_index = device_index;
    out_msg->feature_index = HIDPP_FEATURE_ROOT;
    out_msg->function_id = 0x00; /* getFeature */
    out_msg->software_id = sw_id & 0x0F;
    out_msg->payload_len = 3;
    out_msg->payload[0] = (uint8_t)((feature_id >> 8) & 0xFF);
    out_msg->payload[1] = (uint8_t)(feature_id & 0xFF);
    out_msg->payload[2] = 0x00;
}

bool hidpp_parse_get_feature_response(
    const hidpp_v20_msg_t *resp,
    uint8_t *out_feature_index,
    uint8_t *out_feature_type)
{
    if (!resp || resp->payload_len < 1) {
        return false;
    }

    uint8_t f_idx = resp->payload[0];
    if (f_idx == 0) {
        return false; /* 0 means feature is NOT supported by firmware */
    }

    if (out_feature_index) {
        *out_feature_index = f_idx;
    }
    if (out_feature_type && resp->payload_len >= 2) {
        *out_feature_type = resp->payload[1];
    }

    return true;
}

/* =========================================================================
 * Diagnostics & String Helpers
 * ========================================================================= */

const char *hidpp20_strerror(hidpp20_error_t err)
{
    switch (err) {
        case HIDPP20_ERR_NO_ERROR:              return "No error";
        case HIDPP20_ERR_UNKNOWN:               return "Unknown error";
        case HIDPP20_ERR_INVALID_ARGUMENT:      return "Invalid argument";
        case HIDPP20_ERR_OUT_OF_RANGE:          return "Out of range";
        case HIDPP20_ERR_HARDWARE_ERROR:        return "Hardware error";
        case HIDPP20_ERR_LOGITECH_INTERNAL:     return "Logitech internal error";
        case HIDPP20_ERR_INVALID_FEATURE_INDEX: return "Invalid feature index";
        case HIDPP20_ERR_INVALID_FUNCTION_ID:   return "Invalid function ID";
        case HIDPP20_ERR_BUSY:                  return "Device busy";
        case HIDPP20_ERR_UNSUPPORTED:           return "Feature/function unsupported";
        default:                                return "Unknown HID++ 2.0 error code";
    }
}

const char *hidpp10_strerror(hidpp10_error_t err)
{
    switch (err) {
        case HIDPP10_ERR_SUCCESS:               return "Success";
        case HIDPP10_ERR_INVALID_SUBID:         return "Invalid Sub ID";
        case HIDPP10_ERR_INVALID_ADDRESS:       return "Invalid address";
        case HIDPP10_ERR_INVALID_VALUE:         return "Invalid value";
        case HIDPP10_ERR_CONNECT_FAIL:          return "Connection failed";
        case HIDPP10_ERR_TOO_MANY_DEVICES:      return "Too many devices";
        case HIDPP10_ERR_ALREADY_EXISTS:        return "Device already exists";
        case HIDPP10_ERR_BUSY:                  return "Device busy";
        case HIDPP10_ERR_UNKNOWN_DEVICE:        return "Unknown device";
        case HIDPP10_ERR_RESOURCE_ERROR:        return "Resource error";
        case HIDPP10_ERR_REQUEST_UNAVAILABLE:   return "Request unavailable";
        case HIDPP10_ERR_INVALID_PARAM_VALUE:   return "Invalid parameter value";
        case HIDPP10_ERR_WRONG_PIN_CODE:        return "Wrong PIN code";
        default:                                return "Unknown HID++ 1.0 error code";
    }
}

size_t hidpp_format_packet(const hidpp_raw_report_t *raw, char *buf, size_t buf_size)
{
    if (!raw || !buf || buf_size == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < raw->length && written + 3 < buf_size; i++) {
        written += (size_t)snprintf(buf + written, buf_size - written, "%02X%s",
                                    raw->data[i],
                                    (i + 1 < raw->length) ? " " : "");
    }
    buf[written] = '\0';
    return written;
}
