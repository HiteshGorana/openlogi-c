/**
 * @file capture_plan.h
 * @brief Device capture plan resolution and hardware diversion spec ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_CAPTURE_PLAN_H
#define OPENLOGI_CAPTURE_PLAN_H

#include "config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DIVERT_CIDS 8

#define CID_GESTURE_BUTTON 0x00D0
#define CID_BACK_BUTTON    0x0053
#define CID_FORWARD_BUTTON 0x0056
#define CID_MIDDLE_BUTTON  0x0052

typedef struct {
    uint16_t divert_cids[MAX_DIVERT_CIDS];
    size_t divert_count;
    bool wants_raw_xy;
    uint64_t rearm_generation;
} capture_spec_t;

/**
 * @brief Initialize a blank capture spec.
 */
void capture_spec_init(capture_spec_t *spec);

/**
 * @brief Add a control ID to the diversion spec if not already present.
 */
bool capture_spec_add_cid(capture_spec_t *spec, uint16_t cid);

/**
 * @brief Check if a CID is included in the diversion spec.
 */
bool capture_spec_has_cid(const capture_spec_t *spec, uint16_t cid);

/**
 * @brief Resolve hardware capture requirements from a device's configuration.
 */
void capture_plan_resolve(const config_device_t *dev, capture_spec_t *out_spec);
void capture_plan_resolve_for_app(const config_device_t *dev, const char *app, capture_spec_t *out_spec);
typedef bool (*capture_write_fn)(void *context, uint16_t cid, bool divert, bool raw_xy);
/* Updates confirmed state only on acknowledgement; failed additions roll back. */
bool capture_plan_apply(capture_spec_t *applied, const capture_spec_t *desired,
                        capture_write_fn write, void *context);

/**
 * @brief Check if target specs differ, necessitating a firmware re-arm.
 */
bool capture_spec_needs_rearm(const capture_spec_t *old_spec, const capture_spec_t *new_spec);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_CAPTURE_PLAN_H */
