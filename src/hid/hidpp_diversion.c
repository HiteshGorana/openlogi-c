/**
 * @file hidpp_diversion.c
 * @brief Implementation of Logitech ReprogControls (0x1B04) diversion driver and safe rollback.
 */

#include "hidpp_reprog.h"
#include "hid_transport.h"

#include <string.h>
#include <stdio.h>

#define MAX_ACTIVE_DIVERSIONS 32
#define REPROG_TIMEOUT_MS 1500

typedef struct {
    hid_device_t *dev;
    uint8_t device_index;
    uint8_t feature_index;
    uint8_t sw_id;
    uint16_t cid;
    bool active;
} active_diversion_t;

static active_diversion_t g_diversions[MAX_ACTIVE_DIVERSIONS];
static size_t g_diversion_count = 0;

bool hidpp_reprog_get_count(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint8_t *out_count)
{
    if (!dev || !out_count || feature_index == 0) {
        return false;
    }

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feature_index;
    req.function_id = 0x00; /* getCount */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, REPROG_TIMEOUT_MS)) {
        return false;
    }

    if (resp.proto != HIDPP_PROTO_V20 || resp.u.v20.payload_len < 1) {
        return false;
    }

    *out_count = resp.u.v20.payload[0];
    return true;
}

bool hidpp_reprog_get_cid_info(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint8_t index,
    hidpp_cid_info_t *out_info)
{
    if (!dev || !out_info || feature_index == 0) {
        return false;
    }

    memset(out_info, 0, sizeof(*out_info));

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_LONG;
    req.device_index = device_index;
    req.feature_index = feature_index;
    req.function_id = 0x01; /* getCidInfo */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 16;
    req.payload[0] = index;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, REPROG_TIMEOUT_MS)) {
        return false;
    }

    if (resp.proto != HIDPP_PROTO_V20 || resp.u.v20.payload_len < 6) {
        return false;
    }

    const uint8_t *p = resp.u.v20.payload;
    out_info->cid = (uint16_t)((p[0] << 8) | p[1]);
    out_info->task_id = (uint16_t)((p[2] << 8) | p[3]);
    out_info->flags = (uint16_t)((p[4] << 8) | p[5]);

    return true;
}

bool hidpp_reprog_get_reporting(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid,
    hidpp_cid_reporting_t *out_reporting)
{
    if (!dev || !out_reporting || feature_index == 0) {
        return false;
    }

    memset(out_reporting, 0, sizeof(*out_reporting));

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feature_index;
    req.function_id = 0x02; /* getCidReporting */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;
    req.payload[0] = (uint8_t)((cid >> 8) & 0xFF);
    req.payload[1] = (uint8_t)(cid & 0xFF);

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, REPROG_TIMEOUT_MS)) {
        return false;
    }

    if (resp.proto != HIDPP_PROTO_V20 || resp.u.v20.payload_len < 3) {
        return false;
    }

    const uint8_t *p = resp.u.v20.payload;
    out_reporting->cid = (uint16_t)((p[0] << 8) | p[1]);
    out_reporting->diverted = (p[2] & (1 << 0)) != 0;
    out_reporting->persistently_diverted = (p[2] & (1 << 2)) != 0;
    out_reporting->raw_xy = (p[2] & (1 << 4)) != 0;
    out_reporting->force_raw_xy = (p[2] & (1 << 6)) != 0;

    if (resp.u.v20.payload_len >= 5) {
        out_reporting->remap_cid = (uint16_t)((p[3] << 8) | p[4]);
    }

    return true;
}

bool hidpp_reprog_set_reporting(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid,
    bool divert,
    bool raw_xy)
{
    if (!dev || feature_index == 0) {
        return false;
    }

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_LONG;
    req.device_index = device_index;
    req.feature_index = feature_index;
    req.function_id = 0x03; /* setCidReporting */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 16;
    req.payload[0] = (uint8_t)((cid >> 8) & 0xFF);
    req.payload[1] = (uint8_t)(cid & 0xFF);

    /* Byte 2: Diverted valid bit (bit 1) and value (bit 0) */
    req.payload[2] |= (1 << 1);
    if (divert) {
        req.payload[2] |= (1 << 0);
    }

    /* Byte 2: Raw XY valid bit (bit 5) and value (bit 4) */
    req.payload[2] |= (1 << 5);
    if (raw_xy) {
        req.payload[2] |= (1 << 4);
    }

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, REPROG_TIMEOUT_MS)) {
        return false;
    }

    if (resp.proto != HIDPP_PROTO_V20) {
        return false;
    }

    /* Update safety tracker */
    if (divert) {
        hidpp_diversion_track(dev, device_index, feature_index, sw_id, cid);
    } else {
        hidpp_diversion_untrack(dev, cid);
    }

    return true;
}

bool hidpp_reprog_reset_all(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id)
{
    if (!dev || feature_index == 0) {
        return false;
    }

    hidpp_v20_msg_t req;
    memset(&req, 0, sizeof(req));
    req.report_id = HIDPP_REPORT_SHORT;
    req.device_index = device_index;
    req.feature_index = feature_index;
    req.function_id = 0x05; /* resetAllCidReportSettings */
    req.software_id = sw_id & 0x0F;
    req.payload_len = 3;

    hidpp_msg_t resp;
    if (!hid_transact(dev, &req, &resp, REPROG_TIMEOUT_MS)) {
        return false;
    }

    return (resp.proto == HIDPP_PROTO_V20);
}

bool hidpp_reprog_decode_event(
    const hidpp_v20_msg_t *msg,
    uint8_t expected_feature_index,
    hidpp_reprog_event_t *out_event)
{
    if (!msg || !out_event) {
        return false;
    }

    memset(out_event, 0, sizeof(*out_event));

    if (msg->feature_index != expected_feature_index) {
        return false;
    }

    /* Unsolicited HID++ notifications always have software_id == 0 */
    if (msg->software_id != 0) {
        return false;
    }

    /* Function 0: Diverted buttons pressed event */
    if (msg->function_id == 0x00) {
        out_event->type = REPROG_EVENT_DIVERTED_BUTTONS;
        if (msg->payload_len >= 8) {
            uint8_t count = 0;
            for (int i = 0; i < 4; i++) {
                uint16_t cid = (uint16_t)((msg->payload[i * 2] << 8) | msg->payload[i * 2 + 1]);
                if (cid != 0) {
                    out_event->u.buttons.pressed_cids[count++] = cid;
                }
            }
            out_event->u.buttons.count = count;
            return true;
        }
    }
    /* Function 1: Raw mouse XY delta event */
    else if (msg->function_id == 0x01) {
        out_event->type = REPROG_EVENT_RAW_XY;
        if (msg->payload_len >= 4) {
            out_event->u.raw_xy.dx = (int16_t)((msg->payload[0] << 8) | msg->payload[1]);
            out_event->u.raw_xy.dy = (int16_t)((msg->payload[2] << 8) | msg->payload[3]);
            return true;
        }
    }

    return false;
}

/* =========================================================================
 * Global Diversion Safety Registry
 * ========================================================================= */

void hidpp_diversion_track(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid)
{
    /* Check if already tracked */
    for (size_t i = 0; i < g_diversion_count; i++) {
        if (g_diversions[i].active && g_diversions[i].dev == dev && g_diversions[i].cid == cid) {
            g_diversions[i].device_index = device_index;
            g_diversions[i].feature_index = feature_index;
            g_diversions[i].sw_id = sw_id;
            return;
        }
    }

    /* Find free slot */
    for (size_t i = 0; i < MAX_ACTIVE_DIVERSIONS; i++) {
        if (!g_diversions[i].active) {
            g_diversions[i].dev = dev;
            g_diversions[i].device_index = device_index;
            g_diversions[i].feature_index = feature_index;
            g_diversions[i].sw_id = sw_id;
            g_diversions[i].cid = cid;
            g_diversions[i].active = true;
            if (i >= g_diversion_count) {
                g_diversion_count = i + 1;
            }
            return;
        }
    }
}

void hidpp_diversion_untrack(hid_device_t *dev, uint16_t cid)
{
    for (size_t i = 0; i < g_diversion_count; i++) {
        if (g_diversions[i].active && g_diversions[i].dev == dev && g_diversions[i].cid == cid) {
            g_diversions[i].active = false;
            return;
        }
    }
}

void hidpp_diversion_untrack_device(hid_device_t *dev)
{
    if (!dev) return;
    for (size_t i = 0; i < g_diversion_count; i++) {
        if (g_diversions[i].dev == dev) {
            g_diversions[i].active = false;
            g_diversions[i].dev = NULL;
        }
    }
}

void hidpp_diversion_teardown_all(void)
{
    for (size_t i = 0; i < g_diversion_count; i++) {
        if (g_diversions[i].active && g_diversions[i].dev) {
            /* Synchronously send un-divert packet with raw_xy=false */
            hidpp_reprog_set_reporting(
                g_diversions[i].dev,
                g_diversions[i].device_index,
                g_diversions[i].feature_index,
                g_diversions[i].sw_id,
                g_diversions[i].cid,
                false, /* divert = false */
                false  /* raw_xy = false */
            );
            g_diversions[i].active = false;
        }
    }
    g_diversion_count = 0;
}
