/**
 * @file button_runtime.c
 * @brief Implementation of button runtime state machine ported from openlogi-agent-core.
 */

#include "button_runtime.h"

#include <string.h>

void button_runtime_init(button_runtime_t *rt, button_event_callback_t cb, void *user_data)
{
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
    rt->next_press_id = 1;
    rt->on_event = cb;
    rt->user_data = user_data;
}

static void emit_event(button_runtime_t *rt, button_event_type_t type, const active_press_t *press,
                       trigger_action_type_t action_type, button_end_reason_t end_reason)
{
    if (!rt || !rt->on_event) return;
    button_runtime_event_t ev;
    ev.type = type;
    ev.press = *press;
    ev.action_type = action_type;
    ev.end_reason = end_reason;
    rt->on_event(&ev, rt->user_data);
}

bool button_runtime_down(button_runtime_t *rt, uint32_t button_id, bool has_long_press,
                         uint64_t timestamp_ms, void *target_data)
{
    if (!rt) return false;

    /* 1. Check if same button is already held (lost release recovery) */
    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active && rt->presses[i].button_id == button_id) {
            /* Repress replaces the old press: emit canceled event for old press */
            emit_event(rt, BUTTON_EVT_ENDED, &rt->presses[i], (trigger_action_type_t)0,
                       BUTTON_END_CANCELED_REPEATED_DOWN);
            rt->presses[i].active = false;
            break;
        }
    }

    /* 2. Find free slot */
    int slot_idx = -1;
    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (!rt->presses[i].active) {
            slot_idx = (int)i;
            break;
        }
    }
    if (slot_idx < 0) return false;

    /* 3. Initialize new press */
    active_press_t *slot = &rt->presses[slot_idx];
    slot->button_id = button_id;
    slot->press_id = rt->next_press_id++;
    slot->pressed_at_ms = timestamp_ms;
    slot->has_long_press_binding = has_long_press;
    slot->long_press_fired = false;
    slot->active = true;
    slot->target_data = target_data;

    /* 4. Emit started event */
    emit_event(rt, BUTTON_EVT_STARTED, slot, (trigger_action_type_t)0, (button_end_reason_t)0);
    return true;
}

bool button_runtime_up(button_runtime_t *rt, uint32_t button_id, uint64_t timestamp_ms)
{
    if (!rt) return false;
    (void)timestamp_ms;

    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active && rt->presses[i].button_id == button_id) {
            active_press_t press_copy = rt->presses[i];
            rt->presses[i].active = false;

            /* If long press already fired, suppress short click action */
            if (!press_copy.long_press_fired) {
                emit_event(rt, BUTTON_EVT_TRIGGERED, &press_copy, TRIGGER_ACTION_SHORT_CLICK, (button_end_reason_t)0);
            }

            /* Emit ended event */
            emit_event(rt, BUTTON_EVT_ENDED, &press_copy, (trigger_action_type_t)0, BUTTON_END_RELEASED);
            return true;
        }
    }

    /* Not found: release is idempotent (calling up twice on same button returns false) */
    return false;
}

void button_runtime_tick(button_runtime_t *rt, uint64_t now_ms)
{
    if (!rt) return;

    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (!rt->presses[i].active) continue;

        uint64_t elapsed = (now_ms >= rt->presses[i].pressed_at_ms)
                               ? (now_ms - rt->presses[i].pressed_at_ms)
                               : 0;

        /* Check stale hold timeout (e.g. 30 seconds) */
        if (elapsed >= STALE_HOLD_TIMEOUT_MS) {
            active_press_t copy = rt->presses[i];
            rt->presses[i].active = false;
            emit_event(rt, BUTTON_EVT_ENDED, &copy, (trigger_action_type_t)0, BUTTON_END_CANCELED_STALE_HOLD);
            continue;
        }

        /* Check long press threshold (fires once) */
        if (rt->presses[i].has_long_press_binding && !rt->presses[i].long_press_fired) {
            if (elapsed >= LONG_PRESS_THRESHOLD_MS) {
                rt->presses[i].long_press_fired = true;
                emit_event(rt, BUTTON_EVT_TRIGGERED, &rt->presses[i], TRIGGER_ACTION_LONG_PRESS, (button_end_reason_t)0);
            }
        }
    }
}

bool button_runtime_cancel(button_runtime_t *rt, uint32_t button_id, button_end_reason_t reason)
{
    if (!rt) return false;

    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active && rt->presses[i].button_id == button_id) {
            active_press_t copy = rt->presses[i];
            rt->presses[i].active = false;
            emit_event(rt, BUTTON_EVT_ENDED, &copy, (trigger_action_type_t)0, reason);
            return true;
        }
    }
    return false;
}

void button_runtime_cancel_all(button_runtime_t *rt, button_end_reason_t reason)
{
    if (!rt) return;

    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active) {
            active_press_t copy = rt->presses[i];
            rt->presses[i].active = false;
            emit_event(rt, BUTTON_EVT_ENDED, &copy, (trigger_action_type_t)0, reason);
        }
    }
}

bool button_runtime_is_pressed(const button_runtime_t *rt, uint32_t button_id)
{
    if (!rt) return false;
    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active && rt->presses[i].button_id == button_id) {
            return true;
        }
    }
    return false;
}

size_t button_runtime_active_count(const button_runtime_t *rt)
{
    if (!rt) return 0;
    size_t count = 0;
    for (size_t i = 0; i < MAX_ACTIVE_PRESSES; i++) {
        if (rt->presses[i].active) {
            count++;
        }
    }
    return count;
}
