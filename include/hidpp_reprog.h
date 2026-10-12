/**
 * @file hidpp_reprog.h
 * @brief Logitech HID++ 0x1B04 (ReprogControlsV4 / SpecialKeysMseButtons) driver.
 */

#ifndef OPENLOGI_HIDPP_REPROG_H
#define OPENLOGI_HIDPP_REPROG_H

#include "hidpp.h"
#include "hid_transport.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIDPP_FEATURE_REPROG_CONTROLS 0x1B04

/* Known Control IDs (CIDs) */
#define HIDPP_CID_GESTURE_BUTTON       0x00C3  /* MX Master dedicated thumb gesture button */
#define HIDPP_CID_HAPTIC_PANEL         0x00D6  /* MX Master 4 touch rest */
#define HIDPP_CID_MODE_SHIFT_SMARTSHIFT 0x00C4 /* MX SmartShift mode shift button */
#define HIDPP_CID_DPI_SWITCH_ED        0x00ED  /* DPI switch (G-series/gaming) */
#define HIDPP_CID_DPI_SWITCH_FD        0x00FD  /* DPI switch (standard) */
#define HIDPP_CID_BACK_CLASSIC         0x0053  /* Back button (MX Vertical) */
#define HIDPP_CID_BACK_MULTIPLAT       0x00BD  /* Multiplatform Back */
#define HIDPP_CID_BACK_ALT             0x00CE  /* Multiplatform Back alternate */
#define HIDPP_CID_BACK_GENERIC         0x00DB  /* Generic Back */
#define HIDPP_CID_FORWARD_CLASSIC      0x0056  /* Forward button (MX Vertical) */
#define HIDPP_CID_FORWARD_MULTIPLAT    0x00CF  /* Multiplatform Forward */

/* Control Capability Flags (from getCidInfo) */
#define HIDPP_CID_FLAG_DIVERTABLE      (1 << 0)
#define HIDPP_CID_FLAG_PERSIST_DIVERT  (1 << 1)
#define HIDPP_CID_FLAG_RAW_XY          (1 << 4)
#define HIDPP_CID_FLAG_ANALYTICS       (1 << 5)

/**
 * @brief Metadata for a single reprogrammable control.
 */
typedef struct {
    uint16_t cid;       /* Control ID */
    uint16_t task_id;   /* Default firmware action */
    uint16_t flags;     /* Capability bitfield */
} hidpp_cid_info_t;

/**
 * @brief Current reporting status for a control.
 */
typedef struct {
    uint16_t cid;
    bool diverted;
    bool persistently_diverted;
    bool raw_xy;
    bool force_raw_xy;
    uint16_t remap_cid;
} hidpp_cid_reporting_t;

/**
 * @brief Decoded unsolicited event from 0x1B04 feature.
 */
typedef enum {
    REPROG_EVENT_NONE = 0,
    REPROG_EVENT_DIVERTED_BUTTONS, /* Pressed buttons event (func 0) */
    REPROG_EVENT_RAW_XY            /* Relative pointer delta event (func 1) */
} hidpp_reprog_event_type_t;

typedef struct {
    hidpp_reprog_event_type_t type;
    union {
        struct {
            uint16_t pressed_cids[4]; /* Up to 4 simultaneously held buttons */
            uint8_t count;            /* Number of valid non-zero CIDs */
        } buttons;
        struct {
            int16_t dx;               /* Signed horizontal delta */
            int16_t dy;               /* Signed vertical delta */
        } raw_xy;
    } u;
} hidpp_reprog_event_t;

/* --- ReprogControls Operations --- */

/**
 * @brief Get total number of controls exposed by device.
 */
bool hidpp_reprog_get_count(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint8_t *out_count
);

/**
 * @brief Get control info for a given index (0..count-1).
 */
bool hidpp_reprog_get_cid_info(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint8_t index,
    hidpp_cid_info_t *out_info
);

/**
 * @brief Get reporting state for a control.
 */
bool hidpp_reprog_get_reporting(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid,
    hidpp_cid_reporting_t *out_reporting
);

/**
 * @brief Divert or disarm a control in firmware.
 *
 * @param divert If true, events are sent to software; if false, restored to hardware.
 * @param raw_xy If true, streams raw pointer XY while button is held.
 */
bool hidpp_reprog_set_reporting(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid,
    bool divert,
    bool raw_xy
);

/**
 * @brief Reset all diverted controls to default (Func 5 or manual iteration).
 */
bool hidpp_reprog_reset_all(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id
);

/**
 * @brief Decode an incoming unsolicited HID++ 2.0 message for 0x1B04.
 */
bool hidpp_reprog_decode_event(
    const hidpp_v20_msg_t *msg,
    uint8_t expected_feature_index,
    hidpp_reprog_event_t *out_event
);

/* --- Global Diversion Safety Registry --- */

/**
 * @brief Track a diverted control so it can be restored on exit or crash.
 */
void hidpp_diversion_track(
    hid_device_t *dev,
    uint8_t device_index,
    uint8_t feature_index,
    uint8_t sw_id,
    uint16_t cid
);

/**
 * @brief Remove tracking when control is manually undiverted.
 */
void hidpp_diversion_untrack(hid_device_t *dev, uint16_t cid);

/**
 * @brief Remove all tracked diversions for a device that is disconnecting or being destroyed.
 */
void hidpp_diversion_untrack_device(hid_device_t *dev);

/**
 * @brief Synchronously disarm and restore ALL active diversions.
 * Must be called in SIGINT/SIGTERM handlers and at process exit.
 */
void hidpp_diversion_teardown_all(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HIDPP_REPROG_H */
