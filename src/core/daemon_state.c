/**
 * @file daemon_state.c
 * @brief Runtime state computation and transition helper functions.
 */

#include "daemon_state.h"

openlogi_runtime_state_t daemon_compute_target_state(size_t usable_device_count)
{
    return (usable_device_count > 0) ? RUNTIME_STATE_ACTIVE : RUNTIME_STATE_STANDBY;
}

bool daemon_state_should_transition(openlogi_runtime_state_t current, openlogi_runtime_state_t target)
{
    return current != target;
}

const char *daemon_state_to_string(openlogi_runtime_state_t state)
{
    switch (state) {
        case RUNTIME_STATE_ACTIVE:
            return "active";
        case RUNTIME_STATE_STANDBY:
        default:
            return "standby";
    }
}
