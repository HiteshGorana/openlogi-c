/**
 * @file daemon_state.h
 * @brief Runtime state definitions and reconciliation logic for lightweight standby.
 */

#ifndef OPENLOGI_DAEMON_STATE_H
#define OPENLOGI_DAEMON_STATE_H

#include "ipc_server.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Determine target runtime state based on count of usable online devices.
 */
openlogi_runtime_state_t daemon_compute_target_state(size_t usable_device_count);

/**
 * @brief Check if a transition between current and target state is required.
 */
bool daemon_state_should_transition(openlogi_runtime_state_t current, openlogi_runtime_state_t target);

/**
 * @brief Get human-readable string for runtime state ("standby" or "active").
 */
const char *daemon_state_to_string(openlogi_runtime_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_DAEMON_STATE_H */
