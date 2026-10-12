/**
 * @file hid_mock.h
 * @brief Synthetic HID transport mock for unit testing feature probes and drivers.
 */

#ifndef OPENLOGI_HID_MOCK_H
#define OPENLOGI_HID_MOCK_H

#include "hid_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a mock HID device simulating an MX Master 3S.
 */
hid_device_t *hid_mock_create_mx_master_3s(void);

/**
 * @brief Free a mock HID device.
 */
void hid_mock_destroy(hid_device_t *dev);

/**
 * @brief Get mock current DPI.
 */
uint16_t hid_mock_get_dpi(const hid_device_t *dev);

/**
 * @brief Get mock SmartShift mode and threshold.
 */
void hid_mock_get_smartshift(const hid_device_t *dev, uint8_t *mode, uint8_t *threshold);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HID_MOCK_H */
