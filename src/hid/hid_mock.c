/**
 * @file hid_mock.c
 * @brief Implementation of synthetic HID mock device for testing.
 */

#include "hid_mock.h"
#include "hid_device_internal.h"
#include "hidpp.h"
#include "hidpp_reprog.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MOCK_FEATURE_IDX_ROOT           0x00
#define MOCK_FEATURE_IDX_FEATURE_SET    0x01
#define MOCK_FEATURE_IDX_NAME           0x02
#define MOCK_FEATURE_IDX_BATTERY        0x03
#define MOCK_FEATURE_IDX_DPI            0x04
#define MOCK_FEATURE_IDX_SMARTSHIFT     0x05
#define MOCK_FEATURE_IDX_REPROG         0x06

hid_device_t *hid_mock_create_mx_master_3s(void)
{
    hid_device_t *dev = (hid_device_t *)calloc(1, sizeof(hid_device_t));
    if (!dev) return NULL;

    dev->backend_type = HID_BACKEND_MOCK;
    dev->info.vendor_id = LOGITECH_VENDOR_ID;
    dev->info.product_id = 0xB023; /* MX Master 3S Bluetooth PID */
    dev->info.usage_page = 0xFF00;
    dev->info.usage = 0x0002;
    dev->info.is_long_only = false;
    strncpy(dev->info.product_name, "MX Master 3S", sizeof(dev->info.product_name) - 1);
    strncpy(dev->info.serial_number, "6be9d300", sizeof(dev->info.serial_number) - 1);
    snprintf(dev->info.transport_path, sizeof(dev->info.transport_path), "mock:mx_master_3s");

    dev->mock_dpi = 1200;
    dev->mock_smartshift_mode = 3; /* Auto */
    dev->mock_smartshift_threshold = 14;

    dev->ref_count = 1;
    dev->removed = false;
    pthread_mutex_init(&dev->tx_lock, NULL);

    return dev;
}

void hid_mock_destroy(hid_device_t *dev)
{
    if (dev) {
        hidpp_diversion_untrack_device(dev);
        pthread_mutex_lock(&dev->tx_lock);
        dev->removed = true;
        dev->ref_count--;
        bool should_free = (dev->ref_count <= 0);
        pthread_mutex_unlock(&dev->tx_lock);

        if (should_free) {
            pthread_mutex_destroy(&dev->tx_lock);
            free(dev);
        }
    }
}

uint16_t hid_mock_get_dpi(const hid_device_t *dev)
{
    return dev ? dev->mock_dpi : 0;
}

void hid_mock_get_smartshift(const hid_device_t *dev, uint8_t *mode, uint8_t *threshold)
{
    if (!dev) return;
    if (mode) *mode = dev->mock_smartshift_mode;
    if (threshold) *threshold = dev->mock_smartshift_threshold;
}

/* Simulated response engine for mock device */
bool hid_mock_process_transaction(
    hid_device_t *dev,
    const hidpp_v20_msg_t *req,
    hidpp_msg_t *out_resp)
{
    if (!dev || !req || !out_resp) return false;

    memset(out_resp, 0, sizeof(*out_resp));
    out_resp->proto = HIDPP_PROTO_V20;
    hidpp_v20_msg_t *v20 = &out_resp->u.v20;
    v20->report_id = HIDPP_REPORT_SHORT;
    v20->device_index = req->device_index;
    v20->feature_index = req->feature_index;
    v20->function_id = req->function_id;
    v20->software_id = req->software_id;
    v20->payload_len = 3;

    /* 1. Feature 0x00 (Root) */
    if (req->feature_index == HIDPP_FEATURE_ROOT) {
        if (req->function_id == 0x01) {
            /* Root Ping -> answer Protocol 4 */
            v20->payload[0] = 0x04;
            v20->payload[1] = 0x00;
            v20->payload[2] = 0x00;
            return true;
        } else if (req->function_id == 0x00) {
            /* Root getFeature(feature_id) */
            uint16_t query_id = (uint16_t)((req->payload[0] << 8) | req->payload[1]);
            uint8_t discovered_idx = 0;

            switch (query_id) {
                case HIDPP_FEATURE_ID_FEATURE_SET:
                    discovered_idx = MOCK_FEATURE_IDX_FEATURE_SET;
                    break;
                case HIDPP_FEATURE_ID_DEVICE_NAME:
                    discovered_idx = MOCK_FEATURE_IDX_NAME;
                    break;
                case HIDPP_FEATURE_ID_UNIFIED_BATTERY:
                    discovered_idx = MOCK_FEATURE_IDX_BATTERY;
                    break;
                case HIDPP_FEATURE_ID_ADJUSTABLE_DPI:
                    discovered_idx = MOCK_FEATURE_IDX_DPI;
                    break;
                case HIDPP_FEATURE_ID_SMARTSHIFT:
                    discovered_idx = MOCK_FEATURE_IDX_SMARTSHIFT;
                    break;
                case HIDPP_FEATURE_ID_REPROG_CONTROLS_V4:
                    discovered_idx = MOCK_FEATURE_IDX_REPROG;
                    break;
                default:
                    discovered_idx = 0; /* Unsupported */
                    break;
            }

            v20->payload[0] = discovered_idx;
            v20->payload[1] = 0x00; /* type flags */
            v20->payload[2] = 0x00;
            return true;
        }
    }

    /* 2. Feature DeviceName (0x02 in mock) */
    if (req->feature_index == MOCK_FEATURE_IDX_NAME) {
        const char *mock_name = "MX Master 3S";
        if (req->function_id == 0x00) {
            /* getDeviceNameCount */
            v20->payload[0] = (uint8_t)strlen(mock_name);
            return true;
        } else if (req->function_id == 0x01) {
            /* getDeviceName chunk */
            v20->report_id = HIDPP_REPORT_LONG;
            v20->payload_len = 16;
            size_t name_len = strlen(mock_name);
            uint8_t offset = req->payload[0];
            if (offset < name_len) {
                size_t to_copy = name_len - offset;
                if (to_copy > 16) to_copy = 16;
                memcpy(v20->payload, mock_name + offset, to_copy);
            }
            return true;
        }
    }

    /* 3. Feature UnifiedBattery (0x03 in mock) */
    if (req->feature_index == MOCK_FEATURE_IDX_BATTERY) {
        if (req->function_id == 0x01) {
            /* getBatteryInfo */
            v20->payload[0] = 85;   /* 85% */
            v20->payload[1] = 4;    /* Good */
            v20->payload[2] = 0;    /* Discharging */
            return true;
        }
    }

    /* 4. Feature AdjustableDpi (0x04 in mock) */
    if (req->feature_index == MOCK_FEATURE_IDX_DPI) {
        if (req->function_id == 0x02) {
            /* getSensorDpi */
            v20->payload[0] = 0x00; /* Sensor 0 */
            v20->payload[1] = (uint8_t)((dev->mock_dpi >> 8) & 0xFF);
            v20->payload[2] = (uint8_t)(dev->mock_dpi & 0xFF);
            return true;
        } else if (req->function_id == 0x01) {
            /* getSensorDpiList */
            v20->report_id = HIDPP_REPORT_LONG;
            v20->payload_len = 16;
            v20->payload[0] = 0x00; /* Sensor 0 */
            /* min = 200 (0x00C8), range step = 50 (0xE032), max = 4000 (0x0FA0) */
            v20->payload[1] = 0x00; v20->payload[2] = 0xC8; /* 200 */
            v20->payload[3] = 0xE0; v20->payload[4] = 0x32; /* step 50 */
            v20->payload[5] = 0x0F; v20->payload[6] = 0xA0; /* 4000 */
            return true;
        } else if (req->function_id == 0x03) {
            /* setSensorDpi */
            dev->mock_dpi = (uint16_t)((req->payload[1] << 8) | req->payload[2]);
            return true;
        }
    }

    /* 5. Feature SmartShift (0x05 in mock) */
    if (req->feature_index == MOCK_FEATURE_IDX_SMARTSHIFT) {
        if (req->function_id == 0x00) {
            /* getSmartShiftStatus */
            v20->payload[0] = dev->mock_smartshift_mode;
            v20->payload[1] = dev->mock_smartshift_threshold;
            v20->payload[2] = 14; /* default threshold */
            return true;
        } else if (req->function_id == 0x01) {
            /* setSmartShiftStatus */
            dev->mock_smartshift_mode = req->payload[0];
            dev->mock_smartshift_threshold = req->payload[1];
            return true;
        }
    }

    /* 6. Feature ReprogControls (0x06 in mock) */
    if (req->feature_index == MOCK_FEATURE_IDX_REPROG) {
        if (req->function_id == 0x00) {
            /* getCount -> 3 controls */
            v20->payload[0] = 3;
            return true;
        } else if (req->function_id == 0x01) {
            /* getCidInfo */
            v20->report_id = HIDPP_REPORT_LONG;
            v20->payload_len = 16;
            uint8_t idx = req->payload[0];
            if (idx == 0) {
                /* CID 0x00C3 Gesture Button (Divertable, RawXY) */
                v20->payload[0] = 0x00; v20->payload[1] = 0xC3;
                v20->payload[2] = 0x00; v20->payload[3] = 0x01;
                v20->payload[4] = 0x00; v20->payload[5] = 0x11;
            } else if (idx == 1) {
                /* CID 0x0053 Back Button (Divertable) */
                v20->payload[0] = 0x00; v20->payload[1] = 0x53;
                v20->payload[2] = 0x00; v20->payload[3] = 0x02;
                v20->payload[4] = 0x00; v20->payload[5] = 0x01;
            } else if (idx == 2) {
                /* CID 0x0056 Forward Button (Divertable) */
                v20->payload[0] = 0x00; v20->payload[1] = 0x56;
                v20->payload[2] = 0x00; v20->payload[3] = 0x03;
                v20->payload[4] = 0x00; v20->payload[5] = 0x01;
            }
            return true;
        } else if (req->function_id == 0x02) {
            /* getCidReporting */
            v20->payload[0] = req->payload[0];
            v20->payload[1] = req->payload[1];
            v20->payload[2] = 0x00; /* not diverted initially */
            return true;
        } else if (req->function_id == 0x03) {
            /* setCidReporting */
            v20->report_id = HIDPP_REPORT_LONG;
            v20->payload_len = 16;
            memcpy(v20->payload, req->payload, 16);
            return true;
        } else if (req->function_id == 0x05) {
            /* resetAll */
            return true;
        }
    }

    /* Fallback: unsupported feature */
    v20->feature_index = HIDPP_FEATURE_ERROR;
    v20->payload[0] = req->feature_index;
    v20->payload[1] = (uint8_t)((req->function_id << 4) | (req->software_id & 0x0F));
    v20->payload[2] = HIDPP20_ERR_UNSUPPORTED;
    return true;
}
