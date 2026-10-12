/**
 * @file hook.h
 * @brief OS input interception hook interface (CGEventTap / evdev / WH_MOUSE_LL).
 */

#ifndef OPENLOGI_HOOK_H
#define OPENLOGI_HOOK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOOK_EVENT_BUTTON_DOWN,
    HOOK_EVENT_BUTTON_UP,
    HOOK_EVENT_MOUSE_MOVE,
    HOOK_EVENT_MOUSE_DRAG,
    HOOK_EVENT_SCROLL
} hook_event_type_t;

typedef struct {
    hook_event_type_t type;
    uint32_t button_number; /* 1=Left, 2=Right, 3=Middle, 4=Back, 5=Forward, etc. */
    double x;
    double y;
    double scroll_dx;
    double scroll_dy;
} hook_event_t;

/**
 * @brief Action returned by event listener callback.
 */
typedef enum {
    HOOK_ACTION_PASS = 0,     /* Pass event to OS (zero latency) */
    HOOK_ACTION_SUPPRESS = 1  /* Intercept and suppress event from reaching applications */
} hook_action_t;

/**
 * @brief Callback invoked whenever an input event is trapped.
 */
typedef hook_action_t (*hook_event_callback_t)(const hook_event_t *event, void *user_data);

/**
 * @brief Check if process has required OS permissions to install an input event tap.
 * On macOS: checks Accessibility permission.
 */
bool hook_check_permission(void);

/**
 * @brief Initialize the OS event hook subsystem.
 */
bool hook_init(hook_event_callback_t callback, void *user_data);

/**
 * @brief Start the event interception loop in a dedicated background thread.
 */
bool hook_start(void);

/**
 * @brief Stop the event hook and join the background thread.
 */
void hook_stop(void);

/**
 * @brief Check if the hook is currently running.
 */
bool hook_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_HOOK_H */
