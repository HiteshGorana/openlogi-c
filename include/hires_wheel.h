/**
 * @file hires_wheel.h
 * @brief HID++ 2.0 Feature 0x2121 (Hi-Res Wheel) and Feature 0x2110 (SmartShift) packet payloads.
 */

#ifndef OPENLOGI_HIRES_WHEEL_H
#define OPENLOGI_HIRES_WHEEL_H

#include "hidpp.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEATURE_SMARTSHIFT_0x2110 0x2110
#define FEATURE_HIRES_WHEEL_0x2121 0x2121

#define SMARTSHIFT_MODE_RATCHET   0x01
#define SMARTSHIFT_MODE_FREESPIN  0x02

#define HIRES_FLAG_INVERTED       0x01
#define HIRES_FLAG_REPORTING      0x02

/**
 * @brief Build HID++ 2.0 message for setting hi-res wheel mode (0x2121 fn 1).
 */
bool hires_wheel_build_mode_request(uint8_t feature_idx, uint8_t sw_id,
                                    bool inverted, bool hires_reporting,
                                    hidpp_v20_msg_t *out_msg);

/**
 * @brief Build HID++ 2.0 message for setting SmartShift auto-disengage mode and threshold (0x2110 fn 1).
 */
bool smartshift_build_set_request(uint8_t feature_idx, uint8_t sw_id,
                                  uint8_t mode, uint8_t threshold,
                                  hidpp_v20_msg_t *out_msg);

/**
 * @brief Calculate hi-res multiplier ratio (e.g. 120 / 24 = 5).
 */
uint16_t hires_wheel_calculate_multiplier(uint16_t hires_ticks, uint16_t native_notches);

/**
 * @brief Clamp SmartShift disengage sensitivity threshold to safe hardware range (1..255).
 */
uint8_t smartshift_clamp_threshold(int threshold);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HIRES_WHEEL_H */
