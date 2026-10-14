/**
 * @file hid_device_internal.h
 * @brief Internal definition of struct hid_device shared across platform transports and mock.
 */

#ifndef OPENLOGI_HID_DEVICE_INTERNAL_H
#define OPENLOGI_HID_DEVICE_INTERNAL_H

#include "hid_transport.h"
#include "hidpp.h"

#ifdef __APPLE__
#include <IOKit/hid/IOHIDManager.h>
#endif

typedef enum {
    HID_BACKEND_IOKIT,
    HID_BACKEND_HIDRAW,
    HID_BACKEND_MOCK
} hid_backend_type_t;

struct hid_device {
    hid_backend_type_t backend_type;
    hid_device_info_t info;

#ifdef __APPLE__
    IOHIDDeviceRef io_device;
    uint64_t registry_entry_id;
#endif

    uint8_t input_buffer[HIDPP_VERY_LONG_LEN];
    hid_report_callback_t report_cb;
    void *report_user_data;

    /* Pending transaction state */
    bool has_pending_tx;
    hidpp_v20_msg_t pending_req;
    hidpp_msg_t pending_resp;
    bool pending_resp_ready;

    /* Mock backend state (if backend_type == HID_BACKEND_MOCK) */
    uint16_t mock_dpi;
    uint8_t mock_smartshift_mode;
    uint8_t mock_smartshift_threshold;
};

#endif /* OPENLOGI_HID_DEVICE_INTERNAL_H */
