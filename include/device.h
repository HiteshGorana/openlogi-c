/**
 * @file device.h
 * @brief Logitech device model, feature catalog, and feature probe functions for OpenLogi.
 */

#ifndef OPENLOGI_DEVICE_H
#define OPENLOGI_DEVICE_H

#include "hidpp.h"
#include "hid_transport.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SmartShift scroll wheel operating modes */
typedef enum {
    SMARTSHIFT_MODE_DISABLED    = 0,
    SMARTSHIFT_MODE_FREESPIN    = 1,    /**< Always free-spinning (smooth, hyper-fast) */
    SMARTSHIFT_MODE_RATCHET     = 2,    /**< Always ratchet (clicky) */
    SMARTSHIFT_MODE_AUTO        = 3     /**< Automatic switch from ratchet to free-spin based on torque */
} smartshift_mode_t;

/** Device hardware state and discovered features */
typedef struct {
    char id[64];                        /**< Canonical device ID (e.g. "unit:6be9d300") */
    char name[128];                     /**< Marketing name (e.g. "MX Master 3S") */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_index;               /**< 0xFF for receiver/direct, 1..6 for paired slot */
    hidpp_protocol_version_t proto_version;
    uint8_t sw_id;                      /**< Software ID leased for communication */

    /* Discovered Feature Indices (0 means feature not supported by device) */
    uint8_t feat_root;
    uint8_t feat_feature_set;
    uint8_t feat_name;                  /**< Feature 0x0005 */
    uint8_t feat_battery;               /**< Feature 0x1000 or 0x1004 */
    bool is_unified_battery;            /**< true if 0x1004, false if legacy 0x1000 */
    uint8_t feat_dpi;                   /**< Feature 0x2201 or 0x2205 */
    uint8_t feat_smartshift;            /**< Feature 0x2110 or 0x2111 */
    uint8_t feat_reprog_controls;       /**< Feature 0x1B04 */
    uint8_t feat_hires_wheel;           /**< Feature 0x2121 */
    uint8_t feat_thumbwheel;            /**< Feature 0x2150 */

    /* Battery State */
    uint8_t battery_percent;
    bool battery_charging;

    /* Sensor DPI State */
    uint16_t current_dpi;
    uint16_t min_dpi;
    uint16_t max_dpi;
    uint16_t dpi_step;

    /* SmartShift State */
    smartshift_mode_t smartshift_mode;
    uint8_t smartshift_threshold;       /**< Auto-disengage torque threshold (1..255) */
} openlogi_device_t;

/* =========================================================================
 * Feature Probing & Queries
 * ========================================================================= */

/**
 * @brief Probe a device to discover protocol version and all supported features.
 *
 * @param dev Open HID transport handle.
 * @param device_index Target device index (0xFF for direct, 1..6 for receiver slot).
 * @param sw_id Software ID to use for requests.
 * @param out_device Destination device struct.
 * @return true if device responded and was probed, false on communication failure.
 */
bool openlogi_probe_device(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t sw_id,
    openlogi_device_t *out_device
);

/**
 * @brief Read device marketing name via Feature 0x0005.
 */
bool hidpp_read_device_name(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_name_idx,
    uint8_t sw_id,
    char *out_name,
    size_t max_len
);

/**
 * @brief Read battery percentage and charging state via Feature 0x1004 or 0x1000.
 */
bool hidpp_read_battery(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_battery_idx,
    bool is_unified,
    uint8_t sw_id,
    uint8_t *out_percent,
    bool *out_charging
);

/**
 * @brief Read current sensor DPI and sensor range via Feature 0x2201.
 */
bool hidpp_read_dpi(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_dpi_idx,
    uint8_t sw_id,
    uint16_t *out_current_dpi,
    uint16_t *out_min_dpi,
    uint16_t *out_max_dpi,
    uint16_t *out_dpi_step
);

/**
 * @brief Apply sensor DPI via Feature 0x2201.
 */
bool hidpp_write_dpi(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_dpi_idx,
    uint8_t sw_id,
    uint16_t dpi
);

/**
 * @brief Read SmartShift scroll wheel configuration via Feature 0x2110.
 */
bool hidpp_read_smartshift(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_smartshift_idx,
    uint8_t sw_id,
    smartshift_mode_t *out_mode,
    uint8_t *out_threshold
);

/**
 * @brief Apply SmartShift scroll wheel configuration via Feature 0x2110.
 */
bool hidpp_write_smartshift(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feat_smartshift_idx,
    uint8_t sw_id,
    smartshift_mode_t mode,
    uint8_t threshold
);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_DEVICE_H */
