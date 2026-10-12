/**
 * @file dpi_cycle.c
 * @brief Implementation of DPI cycle and sensor clamping ported from openlogi-agent-core.
 */

#include "dpi_cycle.h"

#include <string.h>

bool dpi_cycle_init(dpi_cycle_state_t *cycle, const uint16_t *presets, size_t count)
{
    if (!cycle || !presets || count == 0 || count > MAX_DPI_PRESETS) return false;
    memset(cycle, 0, sizeof(*cycle));
    for (size_t i = 0; i < count; i++) {
        cycle->presets[i] = presets[i];
    }
    cycle->count = count;
    cycle->active_index = 0;
    cycle->device_online = true;
    return true;
}

uint16_t dpi_cycle_current(const dpi_cycle_state_t *cycle)
{
    if (!cycle || cycle->count == 0 || !cycle->device_online) return 0;
    return cycle->presets[cycle->active_index];
}

uint16_t dpi_cycle_next(dpi_cycle_state_t *cycle)
{
    if (!cycle || cycle->count == 0 || !cycle->device_online) return 0;
    cycle->active_index = (cycle->active_index + 1) % cycle->count;
    return cycle->presets[cycle->active_index];
}

void dpi_cycle_set_online(dpi_cycle_state_t *cycle, bool online)
{
    if (!cycle) return;
    if (cycle->device_online && !online) {
        /* Disconnected: invalidate state */
        cycle->device_online = false;
    } else if (!cycle->device_online && online) {
        /* Reconnected: restore and reset index to 0 */
        cycle->device_online = true;
        cycle->active_index = 0;
    }
}

uint16_t dpi_clamp_sensor(uint16_t requested, uint16_t min_dpi, uint16_t max_dpi, uint16_t step)
{
    if (step == 0) step = 50;
    if (min_dpi == 0) min_dpi = 200;
    if (max_dpi < min_dpi) max_dpi = min_dpi;

    /* Snap to nearest step */
    uint32_t snapped = ((uint32_t)requested + (step / 2)) / step * step;

    if (snapped < min_dpi) snapped = min_dpi;
    if (snapped > max_dpi) snapped = max_dpi;

    return (uint16_t)snapped;
}
