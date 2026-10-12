/**
 * @file hid_transport.h
 * @brief Cross-platform HID transport interface for Logitech HID++ communication.
 */

#ifndef OPENLOGI_HID_TRANSPORT_H
#define OPENLOGI_HID_TRANSPORT_H

#include "hidpp.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGITECH_VENDOR_ID 0x046D

/** Information about a discovered raw HID device node */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t usage_page;
    uint16_t usage;
    char product_name[128];
    char serial_number[64];
    char transport_path[256];
    bool is_long_only;          /**< True for BLE-direct devices (macOS Lift, MX Master over BLE) */
} hid_device_info_t;

/** Opaque handle representing an opened HID device */
typedef struct hid_device hid_device_t;

/** Callback for asynchronously received raw input reports */
typedef void (*hid_report_callback_t)(
    hid_device_t *dev,
    const hidpp_raw_report_t *report,
    void *user_data
);

/** Callback for hotplug device connection / disconnection */
typedef void (*hid_hotplug_callback_t)(
    const hid_device_info_t *info,
    bool connected,
    void *user_data
);

/* =========================================================================
 * Transport Lifecycle & Enumeration
 * ========================================================================= */

/**
 * @brief Initialize the platform HID transport subsystem.
 * @return 0 on success, negative error code on failure.
 */
int hid_transport_init(void);

/**
 * @brief Shutdown the platform HID transport subsystem and free resources.
 */
void hid_transport_shutdown(void);

/**
 * @brief Enumerate currently connected Logitech HID++ device collections.
 *
 * Filters to Logitech vendor collections:
 * - 0xFF00 / 0x0002 (USB, Bolt/Unifying receivers, BT-classic)
 * - 0xFF43 / 0x0202 (Bluetooth Low Energy direct)
 * - 0xFF43 / 0x0602 (G-series wired keyboards)
 *
 * @param out_devices Array to receive discovered device information.
 * @param max_count Maximum entries out_devices can hold.
 * @return Number of matching devices found, or negative on error.
 */
int hid_enumerate(hid_device_info_t *out_devices, size_t max_count);

/**
 * @brief Open a HID device by its transport path.
 *
 * @param transport_path Path/identifier returned from hid_enumerate.
 * @return Pointer to opened hid_device_t, or NULL on failure.
 */
hid_device_t *hid_open(const char *transport_path);

/**
 * @brief Close an opened HID device.
 */
void hid_close(hid_device_t *dev);

/**
 * @brief Retrieve device info from an open device handle.
 */
const hid_device_info_t *hid_device_get_info(const hid_device_t *dev);

/* =========================================================================
 * I/O Operations
 * ========================================================================= */

/**
 * @brief Asynchronously send a raw report to the device.
 *
 * @param dev Open device handle.
 * @param report Raw report to transmit.
 * @return true on success, false on I/O error.
 */
bool hid_send_report(hid_device_t *dev, const hidpp_raw_report_t *report);

/**
 * @brief Synchronously execute a HID++ request-response transaction.
 *
 * Sends req, then waits for a response matching (device_index, feature_index, func_id, sw_id)
 * or a corresponding HID++ error report.
 *
 * @param dev Open device handle.
 * @param req Outgoing request message.
 * @param out_resp Destination for matching response message.
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return true if matching response was received, false on timeout or error.
 */
bool hid_transact(
    hid_device_t *dev,
    const hidpp_v20_msg_t *req,
    hidpp_msg_t *out_resp,
    uint32_t timeout_ms
);

/**
 * @brief Register a callback for incoming asynchronous reports on this device.
 */
bool hid_register_report_callback(
    hid_device_t *dev,
    hid_report_callback_t cb,
    void *user_data
);

/**
 * @brief Start background hotplug monitoring.
 */
bool hid_start_hotplug_monitor(hid_hotplug_callback_t cb, void *user_data);

/**
 * @brief Stop background hotplug monitoring.
 */
void hid_stop_hotplug_monitor(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HID_TRANSPORT_H */
