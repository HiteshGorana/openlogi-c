/**
 * @file button_runtime.h
 * @brief Button runtime state machine ported from openlogi-agent-core.
 */

#ifndef OPENLOGI_BUTTON_RUNTIME_H
#define OPENLOGI_BUTTON_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LONG_PRESS_THRESHOLD_MS 300
#define STALE_HOLD_TIMEOUT_MS   30000
#define MAX_ACTIVE_PRESSES      16

typedef enum {
    BUTTON_EVT_STARTED,
    BUTTON_EVT_TRIGGERED,
    BUTTON_EVT_ENDED
} button_event_type_t;

typedef enum {
    BUTTON_END_RELEASED,
    BUTTON_END_CANCELED_REPEATED_DOWN,
    BUTTON_END_CANCELED_INVALIDATED,
    BUTTON_END_CANCELED_STALE_HOLD
} button_end_reason_t;

typedef enum {
    TRIGGER_ACTION_SHORT_CLICK,
    TRIGGER_ACTION_LONG_PRESS,
    TRIGGER_ACTION_GESTURE
} trigger_action_type_t;

typedef struct {
    uint32_t button_id;
    uint64_t press_id;
    uint64_t pressed_at_ms;
    bool has_long_press_binding;
    bool long_press_fired;
    bool active;
    void *target_data;
} active_press_t;

typedef struct {
    button_event_type_t type;
    active_press_t press;
    trigger_action_type_t action_type;
    button_end_reason_t end_reason;
} button_runtime_event_t;

typedef void (*button_event_callback_t)(const button_runtime_event_t *event, void *user_data);

typedef struct {
    active_press_t presses[MAX_ACTIVE_PRESSES];
    uint64_t next_press_id;
    button_event_callback_t on_event;
    void *user_data;
} button_runtime_t;

void button_runtime_init(button_runtime_t *rt, button_event_callback_t cb, void *user_data);

/**
 * @brief Process button press down event.
 * If button is already pressed (repress without release), cancels previous press first.
 */
bool button_runtime_down(button_runtime_t *rt, uint32_t button_id, bool has_long_press, uint64_t timestamp_ms, void *target_data);

/**
 * @brief Process button release (up) event.
 * If long press already fired, suppresses short click and emits only release.
 * Returns true if an active press was found and released, false if no active press.
 */
bool button_runtime_up(button_runtime_t *rt, uint32_t button_id, uint64_t timestamp_ms);

/**
 * @brief Periodic tick to fire due long presses and cancel stale holds.
 */
void button_runtime_tick(button_runtime_t *rt, uint64_t now_ms);

/**
 * @brief Cancel a specific button press.
 */
bool button_runtime_cancel(button_runtime_t *rt, uint32_t button_id, button_end_reason_t reason);

/**
 * @brief Cancel all active button presses (e.g., on profile switch or app focus change).
 */
void button_runtime_cancel_all(button_runtime_t *rt, button_end_reason_t reason);

/**
 * @brief Check if a button is currently pressed.
 */
bool button_runtime_is_pressed(const button_runtime_t *rt, uint32_t button_id);

/**
 * @brief Count currently active presses.
 */
size_t button_runtime_active_count(const button_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_BUTTON_RUNTIME_H */
