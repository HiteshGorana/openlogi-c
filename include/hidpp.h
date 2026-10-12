/**
 * @file hidpp.h
 * @brief Logitech HID++ 1.0 and 2.0+ protocol framing and packet parser for OpenLogi.
 *
 * Lightweight, zero-allocation C implementation of Logitech's HID++ protocol.
 */

#ifndef OPENLOGI_HIDPP_H
#define OPENLOGI_HIDPP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants & Report IDs
 * ========================================================================= */

#define HIDPP_REPORT_SHORT          0x10    /**< 7-byte short report */
#define HIDPP_REPORT_LONG           0x11    /**< 20-byte long report */
#define HIDPP_REPORT_VERY_LONG      0x12    /**< 64-byte very long report */

#define HIDPP_SHORT_LEN             7
#define HIDPP_LONG_LEN              20
#define HIDPP_VERY_LONG_LEN         64

#define HIDPP_V20_SHORT_PAYLOAD_LEN 3       /**< 7 - 4 header bytes */
#define HIDPP_V20_LONG_PAYLOAD_LEN  16      /**< 20 - 4 header bytes */
#define HIDPP_V20_MAX_PAYLOAD_LEN   60      /**< 64 - 4 header bytes */

#define HIDPP_V10_SHORT_PAYLOAD_LEN 4       /**< 7 - 3 header bytes */
#define HIDPP_V10_LONG_PAYLOAD_LEN  17      /**< 20 - 3 header bytes */

#define HIDPP_DEVICE_RECEIVER       0xFF    /**< Target is receiver or direct USB/BLE */
#define HIDPP_FEATURE_ROOT          0x00    /**< Root feature index */
#define HIDPP_FEATURE_ERROR         0xFF    /**< HID++ 2.0 error feature index */
#define HIDPP_V10_SUB_ID_ERROR      0x8F    /**< HID++ 1.0 error sub ID */

/* Standard Logitech Software ID used by OpenLogi */
#define HIDPP_DEFAULT_SW_ID         0x0A

/* =========================================================================
 * Known HID++ Feature IDs (16-bit)
 * ========================================================================= */

#define HIDPP_FEATURE_ID_ROOT                   0x0000
#define HIDPP_FEATURE_ID_FEATURE_SET            0x0001
#define HIDPP_FEATURE_ID_DEVICE_NAME            0x0005
#define HIDPP_FEATURE_ID_BATTERY_STATUS         0x1000
#define HIDPP_FEATURE_ID_UNIFIED_BATTERY        0x1004
#define HIDPP_FEATURE_ID_CHANGE_HOST            0x1814
#define HIDPP_FEATURE_ID_BACKLIGHT              0x1981
#define HIDPP_FEATURE_ID_BACKLIGHT_V2           0x1982
#define HIDPP_FEATURE_ID_REPROG_CONTROLS        0x1B00
#define HIDPP_FEATURE_ID_REPROG_CONTROLS_V2     0x1B01
#define HIDPP_FEATURE_ID_REPROG_CONTROLS_V3     0x1B03
#define HIDPP_FEATURE_ID_REPROG_CONTROLS_V4     0x1B04
#define HIDPP_FEATURE_ID_PERSISTENT_REMAP       0x1B05
#define HIDPP_FEATURE_ID_WIRELESS_STATUS        0x1D4B
#define HIDPP_FEATURE_ID_SMARTSHIFT             0x2110
#define HIDPP_FEATURE_ID_SMARTSHIFT_ENHANCED    0x2111
#define HIDPP_FEATURE_ID_HIRES_WHEEL            0x2121
#define HIDPP_FEATURE_ID_THUMBWHEEL             0x2150
#define HIDPP_FEATURE_ID_ADJUSTABLE_DPI         0x2201
#define HIDPP_FEATURE_ID_EXTENDED_DPI           0x2205
#define HIDPP_FEATURE_ID_FN_INVERSION           0x40A0
#define HIDPP_FEATURE_ID_FN_INVERSION_V2        0x40A2
#define HIDPP_FEATURE_ID_FN_INVERSION_V3        0x40A3

/* =========================================================================
 * Error Codes
 * ========================================================================= */

/** HID++ 2.0 Error Codes */
typedef enum {
    HIDPP20_ERR_NO_ERROR                = 0x00,
    HIDPP20_ERR_UNKNOWN                 = 0x01,
    HIDPP20_ERR_INVALID_ARGUMENT        = 0x02,
    HIDPP20_ERR_OUT_OF_RANGE            = 0x03,
    HIDPP20_ERR_HARDWARE_ERROR          = 0x04,
    HIDPP20_ERR_LOGITECH_INTERNAL       = 0x05,
    HIDPP20_ERR_INVALID_FEATURE_INDEX   = 0x06,
    HIDPP20_ERR_INVALID_FUNCTION_ID     = 0x07,
    HIDPP20_ERR_BUSY                    = 0x08,
    HIDPP20_ERR_UNSUPPORTED             = 0x09
} hidpp20_error_t;

/** HID++ 1.0 Error Codes (Sub ID 0x8F) */
typedef enum {
    HIDPP10_ERR_SUCCESS                 = 0x00,
    HIDPP10_ERR_INVALID_SUBID           = 0x01,
    HIDPP10_ERR_INVALID_ADDRESS         = 0x02,
    HIDPP10_ERR_INVALID_VALUE           = 0x03,
    HIDPP10_ERR_CONNECT_FAIL            = 0x04,
    HIDPP10_ERR_TOO_MANY_DEVICES        = 0x05,
    HIDPP10_ERR_ALREADY_EXISTS          = 0x06,
    HIDPP10_ERR_BUSY                    = 0x07,
    HIDPP10_ERR_UNKNOWN_DEVICE          = 0x08,
    HIDPP10_ERR_RESOURCE_ERROR          = 0x09,
    HIDPP10_ERR_REQUEST_UNAVAILABLE     = 0x0A,
    HIDPP10_ERR_INVALID_PARAM_VALUE     = 0x0B,
    HIDPP10_ERR_WRONG_PIN_CODE          = 0x0C
} hidpp10_error_t;

/** Protocol version supported by device */
typedef enum {
    HIDPP_PROTO_UNKNOWN                 = 0,
    HIDPP_PROTO_V10                     = 1,
    HIDPP_PROTO_V20                     = 2
} hidpp_protocol_version_t;

/* =========================================================================
 * Packet Structures
 * ========================================================================= */

/** Raw byte buffer representation of a HID report */
typedef struct {
    uint8_t data[HIDPP_VERY_LONG_LEN];
    size_t length;
} hidpp_raw_report_t;

/** Structured HID++ 2.0 message */
typedef struct {
    uint8_t report_id;                  /**< 0x10, 0x11, or 0x12 */
    uint8_t device_index;               /**< 0xFF for receiver/direct, 1..6 for paired slot */
    uint8_t feature_index;              /**< Feature index (0..255) */
    uint8_t function_id;                /**< Function ID (0..15) */
    uint8_t software_id;                /**< Software ID (0..15) */
    size_t payload_len;                 /**< Number of valid bytes in payload */
    uint8_t payload[HIDPP_V20_MAX_PAYLOAD_LEN];
} hidpp_v20_msg_t;

/** Structured HID++ 1.0 message */
typedef struct {
    uint8_t report_id;                  /**< 0x10 or 0x11 */
    uint8_t device_index;               /**< 0xFF or 1..6 */
    uint8_t sub_id;                     /**< Sub ID or register address */
    size_t payload_len;                 /**< Number of valid bytes in payload */
    uint8_t payload[HIDPP_V10_LONG_PAYLOAD_LEN];
} hidpp_v10_msg_t;

/** Unified message structure */
typedef struct {
    hidpp_protocol_version_t proto;
    union {
        hidpp_v20_msg_t v20;
        hidpp_v10_msg_t v10;
    } u;
} hidpp_msg_t;

/** Discovered protocol version information */
typedef struct {
    hidpp_protocol_version_t version;
    uint8_t protocol_num;               /**< e.g., 2, 3, 4 for HID++ 2.0 */
    uint8_t target_sw;                  /**< Target software hint */
} hidpp_version_info_t;

/* =========================================================================
 * Core Packet Framing Functions
 * ========================================================================= */

/**
 * @brief Pack a structured HID++ 2.0 message into a raw report buffer.
 *
 * @param msg Source structured 2.0 message.
 * @param raw Destination raw report buffer.
 * @return true on success, false if invalid arguments or payload too large.
 */
bool hidpp_v20_pack(const hidpp_v20_msg_t *msg, hidpp_raw_report_t *raw);

/**
 * @brief Unpack a raw report buffer into a structured HID++ 2.0 message.
 *
 * @param raw Source raw report buffer.
 * @param msg Destination structured 2.0 message.
 * @return true on success, false if buffer is malformed or too short.
 */
bool hidpp_v20_unpack(const hidpp_raw_report_t *raw, hidpp_v20_msg_t *msg);

/**
 * @brief Pack a structured HID++ 1.0 message into a raw report buffer.
 *
 * @param msg Source structured 1.0 message.
 * @param raw Destination raw report buffer.
 * @return true on success, false if invalid arguments or payload too large.
 */
bool hidpp_v10_pack(const hidpp_v10_msg_t *msg, hidpp_raw_report_t *raw);

/**
 * @brief Unpack a raw report buffer into a structured HID++ 1.0 message.
 *
 * @param raw Source raw report buffer.
 * @param msg Destination structured 1.0 message.
 * @return true on success, false if buffer is malformed or too short.
 */
bool hidpp_v10_unpack(const hidpp_raw_report_t *raw, hidpp_v10_msg_t *msg);

/**
 * @brief Auto-detect and unpack any incoming raw HID++ report.
 *
 * Inspects Report ID, byte count, and message format.
 *
 * @param raw Source raw report buffer.
 * @param out_msg Destination unified message.
 * @return true on success, false if not a valid HID++ packet.
 */
bool hidpp_unpack(const hidpp_raw_report_t *raw, hidpp_msg_t *out_msg);

/* =========================================================================
 * Error Detection & Response Matching
 * ========================================================================= */

/**
 * @brief Check if a HID++ 2.0 message is an error response.
 *
 * @param msg The message to inspect.
 * @param out_feature_index Optional pointer to receive the failing feature index.
 * @param out_func_id Optional pointer to receive failing function ID.
 * @param out_sw_id Optional pointer to receive failing software ID.
 * @param out_err Optional pointer to receive the error code enum.
 * @return true if this message is an error response, false otherwise.
 */
bool hidpp_v20_is_error(
    const hidpp_v20_msg_t *msg,
    uint8_t *out_feature_index,
    uint8_t *out_func_id,
    uint8_t *out_sw_id,
    hidpp20_error_t *out_err
);

/**
 * @brief Check if a HID++ 1.0 message is an error response (Sub ID 0x8F).
 *
 * @param msg The message to inspect.
 * @param out_sub_id Optional pointer to receive failing sub ID / address.
 * @param out_err Optional pointer to receive the error code enum.
 * @return true if this message is an error response, false otherwise.
 */
bool hidpp_v10_is_error(
    const hidpp_v10_msg_t *msg,
    uint8_t *out_sub_id,
    hidpp10_error_t *out_err
);

/**
 * @brief Check if an incoming response matches an outgoing request.
 *
 * Handles both normal response matching and error response matching.
 *
 * @param req Outgoing request message.
 * @param resp Incoming response message.
 * @return true if resp is the matching response for req.
 */
bool hidpp_v20_matches_request(const hidpp_v20_msg_t *req, const hidpp_v20_msg_t *resp);

/* =========================================================================
 * Command Builders & Response Parsers
 * ========================================================================= */

/**
 * @brief Build a Root Ping request packet (Feature 0x0000, Func 0x01).
 *
 * Used to determine whether a device speaks HID++ 1.0 or 2.0+.
 *
 * @param device_index Target device index (0xFF for receiver/direct, 1..6 for slot).
 * @param sw_id Software ID to use (e.g. HIDPP_DEFAULT_SW_ID).
 * @param out_msg Destination message struct.
 */
void hidpp_make_ping(uint8_t device_index, uint8_t sw_id, hidpp_v20_msg_t *out_msg);

/**
 * @brief Parse the response to a Root Ping request.
 *
 * @param req The original ping request message.
 * @param resp The incoming response message.
 * @param out_info Destination version info struct.
 * @return true if version was determined, false if response did not match or invalid.
 */
bool hidpp_parse_ping_response(
    const hidpp_v20_msg_t *req,
    const hidpp_msg_t *resp,
    hidpp_version_info_t *out_info
);

/**
 * @brief Build a Root getFeature request packet (Feature 0x0000, Func 0x00).
 *
 * Queries the dynamic feature index for a known 16-bit feature ID.
 *
 * @param device_index Target device index.
 * @param feature_id 16-bit feature ID (e.g. HIDPP_FEATURE_ID_SMARTSHIFT).
 * @param sw_id Software ID to use.
 * @param out_msg Destination message struct.
 */
void hidpp_make_get_feature(
    uint8_t device_index,
    uint16_t feature_id,
    uint8_t sw_id,
    hidpp_v20_msg_t *out_msg
);

/**
 * @brief Parse the response from a Root getFeature request.
 *
 * @param resp The incoming 2.0 response message.
 * @param out_feature_index Destination for discovered feature index (0 if not supported).
 * @param out_feature_type Optional destination for feature type flags.
 * @return true if successfully parsed, false if unsupported or invalid.
 */
bool hidpp_parse_get_feature_response(
    const hidpp_v20_msg_t *resp,
    uint8_t *out_feature_index,
    uint8_t *out_feature_type
);

/* =========================================================================
 * Diagnostics & String Helpers
 * ========================================================================= */

/**
 * @brief Return human-readable string for HID++ 2.0 error code.
 */
const char *hidpp20_strerror(hidpp20_error_t err);

/**
 * @brief Return human-readable string for HID++ 1.0 error code.
 */
const char *hidpp10_strerror(hidpp10_error_t err);

/**
 * @brief Format a raw packet into a hex string for logging/debugging.
 *
 * @param raw Raw report to format.
 * @param buf Destination character buffer.
 * @param buf_size Size of destination buffer (must be at least raw->length * 3 + 1).
 * @return Number of characters written.
 */
size_t hidpp_format_packet(const hidpp_raw_report_t *raw, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HIDPP_H */
