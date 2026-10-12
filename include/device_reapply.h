/**
 * @file device_reapply.h
 * @brief Device volatile settings re-application logic ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_DEVICE_REAPPLY_H
#define OPENLOGI_DEVICE_REAPPLY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOLATILE_REAPPLY_CONFIRM_RETRIES 2
#define MAX_REAPPLY_DEVICES 16

typedef struct {
    char device_key[64];
    uint8_t slot;
    bool online;
} reapply_device_t;

typedef struct {
    size_t target_indices[MAX_REAPPLY_DEVICES];
    size_t target_count;
} reapply_plan_t;

/**
 * @brief Determine which device indices require re-application of volatile settings.
 *
 * @param prev Array of devices from previous inventory tick.
 * @param prev_count Number of devices in previous tick.
 * @param next Array of devices in current inventory tick.
 * @param next_count Number of devices in current tick.
 * @param is_system_wake True if host just resumed from system sleep.
 * @param out_plan Output structure populated with target indices.
 */
void device_reapply_plan(
    const reapply_device_t *prev, size_t prev_count,
    const reapply_device_t *next, size_t next_count,
    bool is_system_wake,
    reapply_plan_t *out_plan
);

/**
 * @brief Helper to check if a specific index is contained in plan.
 */
bool reapply_plan_contains(const reapply_plan_t *plan, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_DEVICE_REAPPLY_H */
