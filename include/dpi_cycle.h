/**
 * @file dpi_cycle.h
 * @brief DPI cycle and sensor clamping logic ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_DPI_CYCLE_H
#define OPENLOGI_DPI_CYCLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DPI_PRESETS 16

typedef struct {
    uint16_t presets[MAX_DPI_PRESETS];
    size_t count;
    size_t active_index;
    bool device_online;
} dpi_cycle_state_t;

/**
 * @brief Initialize a DPI cycle state with presets.
 */
bool dpi_cycle_init(dpi_cycle_state_t *cycle, const uint16_t *presets, size_t count);

/**
 * @brief Get the currently active DPI preset.
 */
uint16_t dpi_cycle_current(const dpi_cycle_state_t *cycle);

/**
 * @brief Advance to next DPI preset, wrapping around to 0 on overflow.
 */
uint16_t dpi_cycle_next(dpi_cycle_state_t *cycle);

/**
 * @brief Handle device online/offline transitions.
 * When device goes offline, cycle state is cleared.
 * When device returns online, resets to index 0.
 */
void dpi_cycle_set_online(dpi_cycle_state_t *cycle, bool online);

/**
 * @brief Clamp and snap requested DPI to sensor limits and step size.
 */
uint16_t dpi_clamp_sensor(uint16_t requested, uint16_t min_dpi, uint16_t max_dpi, uint16_t step);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_DPI_CYCLE_H */
