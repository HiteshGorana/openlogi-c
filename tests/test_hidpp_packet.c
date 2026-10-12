/**
 * @file test_hidpp_packet.c
 * @brief Unit tests for HID++ protocol packet framing, serialization, and parsing.
 */

#include "hidpp.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

TEST_SUITE_DECLARE(g_suite);

/* =========================================================================
 * Test Cases
 * ========================================================================= */

static void test_v20_short_pack_unpack(void)
{
    hidpp_v20_msg_t in_msg;
    memset(&in_msg, 0, sizeof(in_msg));
    in_msg.device_index = 0x01;
    in_msg.feature_index = 0x05;
    in_msg.function_id = 0x02;
    in_msg.software_id = 0x0A;
    in_msg.payload_len = 3;
    in_msg.payload[0] = 0xAA;
    in_msg.payload[1] = 0xBB;
    in_msg.payload[2] = 0xCC;

    hidpp_raw_report_t raw;
    bool ok = hidpp_v20_pack(&in_msg, &raw);
    TEST_ASSERT(ok, "Packing short v20 message should succeed");
    TEST_ASSERT(raw.length == HIDPP_SHORT_LEN, "Short report length must be 7");
    TEST_ASSERT(raw.data[0] == HIDPP_REPORT_SHORT, "Report ID must be 0x10");
    TEST_ASSERT(raw.data[1] == 0x01, "Device index must be 0x01");
    TEST_ASSERT(raw.data[2] == 0x05, "Feature index must be 0x05");
    TEST_ASSERT(raw.data[3] == 0x2A, "Byte 3 must be (2 << 4) | 0x0A = 0x2A");
    TEST_ASSERT(raw.data[4] == 0xAA, "Payload[0] mismatch");
    TEST_ASSERT(raw.data[5] == 0xBB, "Payload[1] mismatch");
    TEST_ASSERT(raw.data[6] == 0xCC, "Payload[2] mismatch");

    /* Unpack test */
    hidpp_v20_msg_t out_msg;
    ok = hidpp_v20_unpack(&raw, &out_msg);
    TEST_ASSERT(ok, "Unpacking short v20 message should succeed");
    TEST_ASSERT(out_msg.report_id == HIDPP_REPORT_SHORT, "Report ID mismatch in unpacked");
    TEST_ASSERT(out_msg.device_index == 0x01, "Device index mismatch in unpacked");
    TEST_ASSERT(out_msg.feature_index == 0x05, "Feature index mismatch in unpacked");
    TEST_ASSERT(out_msg.function_id == 0x02, "Function ID mismatch in unpacked");
    TEST_ASSERT(out_msg.software_id == 0x0A, "Software ID mismatch in unpacked");
    TEST_ASSERT(out_msg.payload_len == 3, "Payload len mismatch in unpacked");
    TEST_ASSERT(memcmp(in_msg.payload, out_msg.payload, 3) == 0, "Payload data mismatch");
}

static void test_v20_long_pack_unpack(void)
{
    hidpp_v20_msg_t in_msg;
    memset(&in_msg, 0, sizeof(in_msg));
    in_msg.device_index = HIDPP_DEVICE_RECEIVER;
    in_msg.feature_index = 0x08;
    in_msg.function_id = 0x01;
    in_msg.software_id = 0x0E;
    in_msg.payload_len = 16;
    for (size_t i = 0; i < 16; i++) {
        in_msg.payload[i] = (uint8_t)(i + 1);
    }

    hidpp_raw_report_t raw;
    bool ok = hidpp_v20_pack(&in_msg, &raw);
    TEST_ASSERT(ok, "Packing long v20 message should succeed");
    TEST_ASSERT(raw.length == HIDPP_LONG_LEN, "Long report length must be 20");
    TEST_ASSERT(raw.data[0] == HIDPP_REPORT_LONG, "Report ID must be 0x11");
    TEST_ASSERT(raw.data[1] == 0xFF, "Device index must be 0xFF");
    TEST_ASSERT(raw.data[2] == 0x08, "Feature index must be 0x08");
    TEST_ASSERT(raw.data[3] == 0x1E, "Func/SW ID must be 0x1E");

    hidpp_v20_msg_t out_msg;
    ok = hidpp_v20_unpack(&raw, &out_msg);
    TEST_ASSERT(ok, "Unpacking long v20 message should succeed");
    TEST_ASSERT(out_msg.report_id == HIDPP_REPORT_LONG, "Report ID mismatch");
    TEST_ASSERT(out_msg.payload_len == 16, "Payload length must be 16");
    TEST_ASSERT(memcmp(in_msg.payload, out_msg.payload, 16) == 0, "Payload mismatch");
}

static void test_v20_very_long_pack_unpack(void)
{
    hidpp_v20_msg_t in_msg;
    memset(&in_msg, 0, sizeof(in_msg));
    in_msg.device_index = 0x02;
    in_msg.feature_index = 0x1B;
    in_msg.function_id = 0x03;
    in_msg.software_id = 0x01;
    in_msg.payload_len = 40;
    for (size_t i = 0; i < 40; i++) {
        in_msg.payload[i] = (uint8_t)(0x80 | i);
    }

    hidpp_raw_report_t raw;
    bool ok = hidpp_v20_pack(&in_msg, &raw);
    TEST_ASSERT(ok, "Packing very long v20 message should succeed");
    TEST_ASSERT(raw.length == HIDPP_VERY_LONG_LEN, "Very long report length must be 64");
    TEST_ASSERT(raw.data[0] == HIDPP_REPORT_VERY_LONG, "Report ID must be 0x12");

    hidpp_v20_msg_t out_msg;
    ok = hidpp_v20_unpack(&raw, &out_msg);
    TEST_ASSERT(ok, "Unpacking very long v20 message should succeed");
    TEST_ASSERT(out_msg.report_id == HIDPP_REPORT_VERY_LONG, "Report ID mismatch");
    TEST_ASSERT(out_msg.payload_len == 60, "Available payload capacity is 60");
    TEST_ASSERT(memcmp(in_msg.payload, out_msg.payload, 40) == 0, "Payload mismatch");
}

static void test_v10_pack_unpack(void)
{
    hidpp_v10_msg_t in_msg;
    memset(&in_msg, 0, sizeof(in_msg));
    in_msg.device_index = 0x01;
    in_msg.sub_id = 0x00; /* Register read */
    in_msg.payload_len = 4;
    in_msg.payload[0] = 0x0B;
    in_msg.payload[1] = 0x02;
    in_msg.payload[2] = 0x00;
    in_msg.payload[3] = 0x00;

    hidpp_raw_report_t raw;
    bool ok = hidpp_v10_pack(&in_msg, &raw);
    TEST_ASSERT(ok, "Packing v10 short should succeed");
    TEST_ASSERT(raw.length == HIDPP_SHORT_LEN, "Length must be 7");
    TEST_ASSERT(raw.data[0] == HIDPP_REPORT_SHORT, "Report ID must be 0x10");
    TEST_ASSERT(raw.data[1] == 0x01, "Device index must be 0x01");
    TEST_ASSERT(raw.data[2] == 0x00, "Sub ID must be 0x00");

    hidpp_v10_msg_t out_msg;
    ok = hidpp_v10_unpack(&raw, &out_msg);
    TEST_ASSERT(ok, "Unpacking v10 short should succeed");
    TEST_ASSERT(out_msg.device_index == 0x01, "Device index mismatch");
    TEST_ASSERT(out_msg.sub_id == 0x00, "Sub ID mismatch");
    TEST_ASSERT(memcmp(in_msg.payload, out_msg.payload, 4) == 0, "Payload mismatch");
}

static void test_v20_error_detection(void)
{
    /* Raw response: 11 01 FF 05 03 1A 02 00 00 ... */
    /* Device 1, Feature 0xFF (Error), Sub-Feature 0x03, Func 1, SW 0x0A, Error 0x02 (Invalid Arg) */
    hidpp_raw_report_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.length = HIDPP_LONG_LEN;
    raw.data[0] = HIDPP_REPORT_LONG;
    raw.data[1] = 0x01;
    raw.data[2] = HIDPP_FEATURE_ERROR; /* 0xFF */
    raw.data[3] = 0x00;
    raw.data[4] = 0x03; /* Failing feature index */
    raw.data[5] = 0x1A; /* (func 1 << 4) | sw 0x0A */
    raw.data[6] = 0x02; /* Error: Invalid Argument */

    hidpp_v20_msg_t msg;
    bool ok = hidpp_v20_unpack(&raw, &msg);
    TEST_ASSERT(ok, "Unpacking error packet should succeed");

    uint8_t err_feature = 0;
    uint8_t err_func = 0;
    uint8_t err_sw = 0;
    hidpp20_error_t err_code = HIDPP20_ERR_NO_ERROR;

    bool is_err = hidpp_v20_is_error(&msg, &err_feature, &err_func, &err_sw, &err_code);
    TEST_ASSERT(is_err, "Must be recognized as HID++ 2.0 error");
    TEST_ASSERT(err_feature == 0x03, "Failing feature index must be 0x03");
    TEST_ASSERT(err_func == 0x01, "Failing function must be 1");
    TEST_ASSERT(err_sw == 0x0A, "Failing software ID must be 0x0A");
    TEST_ASSERT(err_code == HIDPP20_ERR_INVALID_ARGUMENT, "Error code must be Invalid Argument");

    const char *err_str = hidpp20_strerror(err_code);
    TEST_ASSERT(strcmp(err_str, "Invalid argument") == 0, "Error string must match");
}

static void test_v10_error_detection(void)
{
    /* Raw response: 10 01 8F 00 1A 01 00 */
    /* Sub ID 0x8F, failed sub_id 0x00, register 0x1A, error 0x01 (Invalid SubID) */
    hidpp_raw_report_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.length = HIDPP_SHORT_LEN;
    raw.data[0] = HIDPP_REPORT_SHORT;
    raw.data[1] = 0x01;
    raw.data[2] = HIDPP_V10_SUB_ID_ERROR; /* 0x8F */
    raw.data[3] = 0x00;
    raw.data[4] = 0x1A;
    raw.data[5] = 0x01; /* Error: Invalid Sub ID */
    raw.data[6] = 0x00;

    hidpp_v10_msg_t msg;
    bool ok = hidpp_v10_unpack(&raw, &msg);
    TEST_ASSERT(ok, "Unpacking v10 error packet should succeed");

    uint8_t sub_id = 0;
    hidpp10_error_t err_code = HIDPP10_ERR_SUCCESS;
    bool is_err = hidpp_v10_is_error(&msg, &sub_id, &err_code);
    TEST_ASSERT(is_err, "Must be recognized as HID++ 1.0 error");
    TEST_ASSERT(sub_id == 0x00, "Failed Sub ID must be 0x00");
    TEST_ASSERT(err_code == HIDPP10_ERR_INVALID_SUBID, "Error code must be Invalid Sub ID");

    const char *err_str = hidpp10_strerror(err_code);
    TEST_ASSERT(strcmp(err_str, "Invalid Sub ID") == 0, "Error string must match");
}

static void test_request_response_matching(void)
{
    /* Outgoing request: Device 1, Feature 0x07, Func 2, SW 0x0A */
    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.device_index = 0x01;
    req.feature_index = 0x07;
    req.function_id = 0x02;
    req.software_id = 0x0A;

    /* 1. Exact match */
    hidpp_v20_msg_t resp_match;
    memset(&resp_match, 0, sizeof(resp_match));
    resp_match.device_index = 0x01;
    resp_match.feature_index = 0x07;
    resp_match.function_id = 0x02;
    resp_match.software_id = 0x0A;
    TEST_ASSERT(hidpp_v20_matches_request(&req, &resp_match), "Matching response must match");

    /* 2. Device mismatch */
    hidpp_v20_msg_t resp_dev_mismatch = resp_match;
    resp_dev_mismatch.device_index = 0x02;
    TEST_ASSERT(!hidpp_v20_matches_request(&req, &resp_dev_mismatch), "Different device must NOT match");

    /* 3. Feature mismatch */
    hidpp_v20_msg_t resp_feat_mismatch = resp_match;
    resp_feat_mismatch.feature_index = 0x08;
    TEST_ASSERT(!hidpp_v20_matches_request(&req, &resp_feat_mismatch), "Different feature must NOT match");

    /* 4. Software ID mismatch */
    hidpp_v20_msg_t resp_sw_mismatch = resp_match;
    resp_sw_mismatch.software_id = 0x0B;
    TEST_ASSERT(!hidpp_v20_matches_request(&req, &resp_sw_mismatch), "Different SW ID must NOT match");

    /* 5. Error matching response */
    hidpp_v20_msg_t resp_err;
    memset(&resp_err, 0, sizeof(resp_err));
    resp_err.device_index = 0x01;
    resp_err.feature_index = HIDPP_FEATURE_ERROR; /* 0xFF */
    resp_err.payload_len = 3;
    resp_err.payload[0] = 0x07; /* Matches req.feature_index */
    resp_err.payload[1] = (0x02 << 4) | 0x0A; /* Matches func 2, sw 0x0A */
    resp_err.payload[2] = HIDPP20_ERR_BUSY;
    TEST_ASSERT(hidpp_v20_matches_request(&req, &resp_err), "Matching error response must match request");
}

static void test_ping_and_version_discovery(void)
{
    hidpp_v20_msg_t ping_req;
    hidpp_make_ping(0x01, HIDPP_DEFAULT_SW_ID, &ping_req);

    TEST_ASSERT(ping_req.device_index == 0x01, "Device index must be 1");
    TEST_ASSERT(ping_req.feature_index == 0x00, "Root feature index must be 0");
    TEST_ASSERT(ping_req.function_id == 0x01, "Ping func ID must be 1");
    TEST_ASSERT(ping_req.software_id == HIDPP_DEFAULT_SW_ID, "SW ID mismatch");

    /* Test Case A: Device responds with HID++ 2.0 ping response (Protocol 4 = Options+) */
    hidpp_msg_t v20_resp;
    memset(&v20_resp, 0, sizeof(v20_resp));
    v20_resp.proto = HIDPP_PROTO_V20;
    v20_resp.u.v20.device_index = 0x01;
    v20_resp.u.v20.feature_index = 0x00;
    v20_resp.u.v20.function_id = 0x01;
    v20_resp.u.v20.software_id = HIDPP_DEFAULT_SW_ID;
    v20_resp.u.v20.payload_len = 3;
    v20_resp.u.v20.payload[0] = 0x04; /* Protocol num 4 */
    v20_resp.u.v20.payload[1] = 0x00; /* Target SW */
    v20_resp.u.v20.payload[2] = 0x00;

    hidpp_version_info_t info;
    bool ok = hidpp_parse_ping_response(&ping_req, &v20_resp, &info);
    TEST_ASSERT(ok, "Ping parse should succeed for v20");
    TEST_ASSERT(info.version == HIDPP_PROTO_V20, "Protocol version must be V20");
    TEST_ASSERT(info.protocol_num == 4, "Protocol num must be 4");

    /* Test Case B: Device responds with HID++ 1.0 error (SubID 0x8F, invalid sub_id) */
    hidpp_msg_t v10_resp;
    memset(&v10_resp, 0, sizeof(v10_resp));
    v10_resp.proto = HIDPP_PROTO_V10;
    v10_resp.u.v10.device_index = 0x01;
    v10_resp.u.v10.sub_id = HIDPP_V10_SUB_ID_ERROR;
    v10_resp.u.v10.payload_len = 4;
    v10_resp.u.v10.payload[0] = 0x00; /* Sent feature index 0x00 */
    v10_resp.u.v10.payload[1] = (0x01 << 4) | HIDPP_DEFAULT_SW_ID;
    v10_resp.u.v10.payload[2] = HIDPP10_ERR_INVALID_SUBID;

    ok = hidpp_parse_ping_response(&ping_req, &v10_resp, &info);
    TEST_ASSERT(ok, "Ping parse should succeed for v10 fallback");
    TEST_ASSERT(info.version == HIDPP_PROTO_V10, "Protocol version must be V10");
}

static void test_feature_lookup(void)
{
    hidpp_v20_msg_t get_feat_req;
    hidpp_make_get_feature(0x01, HIDPP_FEATURE_ID_SMARTSHIFT, HIDPP_DEFAULT_SW_ID, &get_feat_req);

    TEST_ASSERT(get_feat_req.device_index == 0x01, "Device index must be 1");
    TEST_ASSERT(get_feat_req.feature_index == 0x00, "Root feature index must be 0");
    TEST_ASSERT(get_feat_req.function_id == 0x00, "getFeature func must be 0");
    TEST_ASSERT(get_feat_req.payload[0] == 0x21, "Feature ID MSB must be 0x21");
    TEST_ASSERT(get_feat_req.payload[1] == 0x10, "Feature ID LSB must be 0x10");

    /* Response: Feature index is 0x06, type is 0x00 */
    hidpp_v20_msg_t feat_resp;
    memset(&feat_resp, 0, sizeof(feat_resp));
    feat_resp.payload_len = 3;
    feat_resp.payload[0] = 0x06; /* Feature index */
    feat_resp.payload[1] = 0x00; /* Feature type */
    feat_resp.payload[2] = 0x00;

    uint8_t out_idx = 0;
    uint8_t out_type = 0;
    bool ok = hidpp_parse_get_feature_response(&feat_resp, &out_idx, &out_type);
    TEST_ASSERT(ok, "getFeature parse should succeed");
    TEST_ASSERT(out_idx == 0x06, "Discovered feature index must be 0x06");

    /* Response with index 0 (feature unsupported) */
    feat_resp.payload[0] = 0x00;
    ok = hidpp_parse_get_feature_response(&feat_resp, &out_idx, &out_type);
    TEST_ASSERT(!ok, "Feature index 0 means unsupported");
}

static void test_hex_packet_formatting(void)
{
    hidpp_raw_report_t raw;
    raw.length = 7;
    raw.data[0] = 0x10;
    raw.data[1] = 0x01;
    raw.data[2] = 0x00;
    raw.data[3] = 0x1A;
    raw.data[4] = 0x00;
    raw.data[5] = 0x00;
    raw.data[6] = 0x00;

    char buf[64];
    size_t len = hidpp_format_packet(&raw, buf, sizeof(buf));
    TEST_ASSERT(len > 0, "Packet formatter should return length");
    TEST_ASSERT(strcmp(buf, "10 01 00 1A 00 00 00") == 0, "Hex dump must match format");
}

/* =========================================================================
 * Main Runner
 * ========================================================================= */

int main(void)
{
    test_suite_begin(&g_suite, "test_hidpp_packet", "HID++ 1.0 & 2.0 Framing & Protocol Engine");

    TEST_CASE_RUN(g_suite, test_v20_short_pack_unpack);
    TEST_CASE_RUN(g_suite, test_v20_long_pack_unpack);
    TEST_CASE_RUN(g_suite, test_v20_very_long_pack_unpack);
    TEST_CASE_RUN(g_suite, test_v10_pack_unpack);
    TEST_CASE_RUN(g_suite, test_v20_error_detection);
    TEST_CASE_RUN(g_suite, test_v10_error_detection);
    TEST_CASE_RUN(g_suite, test_request_response_matching);
    TEST_CASE_RUN(g_suite, test_ping_and_version_discovery);
    TEST_CASE_RUN(g_suite, test_feature_lookup);
    TEST_CASE_RUN(g_suite, test_hex_packet_formatting);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
