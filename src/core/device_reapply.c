/**
 * @file device_reapply.c
 * @brief Implementation of device re-application transitions ported from openlogi-agent-core.
 */

#include "device_reapply.h"

#include <string.h>

void device_reapply_plan(
    const reapply_device_t *prev, size_t prev_count,
    const reapply_device_t *next, size_t next_count,
    bool is_system_wake,
    reapply_plan_t *out_plan)
{
    if (!out_plan) return;
    out_plan->target_count = 0;

    for (size_t i = 0; i < next_count; i++) {
        /* Invariant: offline / sleeping devices are always skipped */
        if (!next[i].online) {
            continue;
        }

        /* Invariant: on system wake, all currently online devices are targeted */
        if (is_system_wake) {
            if (out_plan->target_count < MAX_REAPPLY_DEVICES) {
                out_plan->target_indices[out_plan->target_count++] = i;
            }
            continue;
        }

        /* Find matching device in prev by device_key and slot */
        const reapply_device_t *prev_match = NULL;
        for (size_t p = 0; p < prev_count; p++) {
            if (prev[p].slot == next[i].slot &&
                strcmp(prev[p].device_key, next[i].device_key) == 0) {
                prev_match = &prev[p];
                break;
            }
        }

        if (!prev_match) {
            /* First sighting or migrated slot -> target */
            if (out_plan->target_count < MAX_REAPPLY_DEVICES) {
                out_plan->target_indices[out_plan->target_count++] = i;
            }
        } else if (!prev_match->online && next[i].online) {
            /* Waking from device sleep (offline -> online) -> target */
            if (out_plan->target_count < MAX_REAPPLY_DEVICES) {
                out_plan->target_indices[out_plan->target_count++] = i;
            }
        }
        /* Steady state (prev_match->online == true and next[i].online == true) -> do nothing */
    }
}

bool reapply_plan_contains(const reapply_plan_t *plan, size_t index)
{
    if (!plan) return false;
    for (size_t i = 0; i < plan->target_count; i++) {
        if (plan->target_indices[i] == index) {
            return true;
        }
    }
    return false;
}
