/**
 * @file inject.h
 * @brief OS input injection interface for actions, gestures, and keystrokes.
 */

#ifndef OPENLOGI_INJECT_H
#define OPENLOGI_INJECT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACTION_NONE = 0,
    ACTION_KEY_COMBO,
    ACTION_MEDIA_KEY,
    ACTION_MISSION_CONTROL,
    ACTION_APP_EXPOSE,
    ACTION_SHOW_DESKTOP,
    ACTION_LAUNCHPAD,
    ACTION_NEXT_SPACE,
    ACTION_PREV_SPACE,
    ACTION_MOUSE_CLICK,
    ACTION_SCROLL,
    ACTION_SHELL_COMMAND,
    ACTION_TEXT,
    ACTION_APP_OPEN,
    ACTION_APPLE_SCRIPT,
    ACTION_SLEEP,
    ACTION_LOCK_SCREEN,
    ACTION_SCREENSHOT,
    ACTION_CAPTURE_REGION,
    ACTION_CYCLE_DPI,
    ACTION_SET_DPI_PRESET,
    ACTION_TOGGLE_SMARTSHIFT,
    ACTION_SHOW_ACTIONS_RING,
    ACTION_WORKFLOW
} action_type_t;

typedef enum {
    MEDIA_KEY_PLAY_PAUSE = 0,
    MEDIA_KEY_NEXT,
    MEDIA_KEY_PREVIOUS,
    MEDIA_KEY_VOLUME_UP,
    MEDIA_KEY_VOLUME_DOWN,
    MEDIA_KEY_MUTE
} media_key_t;

#define MOD_CMD   (1 << 0)
#define MOD_CTRL  (1 << 1)
#define MOD_ALT   (1 << 2)
#define MOD_SHIFT (1 << 3)

#define MAX_WORKFLOW_STEPS 16

typedef enum {
    WORKFLOW_STEP_PRESS_KEY = 0,
    WORKFLOW_STEP_DELAY,
    WORKFLOW_STEP_TYPE_TEXT,
    WORKFLOW_STEP_APPLE_SCRIPT,
    WORKFLOW_STEP_SHELL_CMD
} workflow_step_type_t;

typedef struct {
    workflow_step_type_t type;
    union {
        struct {
            uint16_t keycode;
            uint32_t modifiers;
        } key;
        uint32_t delay_ms;
        char text[128];
        char script[256];
        char cmd[256];
    } u;
} workflow_step_t;

typedef struct {
    workflow_step_t steps[MAX_WORKFLOW_STEPS];
    size_t step_count;
} openlogi_workflow_t;

typedef struct {
    action_type_t type;
    union {
        struct {
            uint16_t keycode;
            uint32_t modifiers;
            bool hold;
        } key_combo;
        media_key_t media_key;
        struct {
            uint32_t button;
        } mouse_click;
        struct {
            double dx;
            double dy;
        } scroll;
        char text[128];
        char shell_cmd[128];
        char app_path[128];
        char apple_script[128];
        uint8_t dpi_preset_index;
        openlogi_workflow_t *workflow;
    } u;
} openlogi_action_t;

/**
 * @brief Initialize OS injection subsystem.
 */
bool inject_init(void);

/**
 * @brief Inject a synthesized keyboard shortcut with modifiers.
 */
void inject_key_combo(uint16_t keycode, uint32_t modifiers);

/**
 * @brief Inject a system media key (Play, Pause, Next, Volume Up/Down, Mute).
 */
void inject_media_key(media_key_t key);

/**
 * @brief Trigger Mission Control.
 */
void inject_mission_control(void);

/**
 * @brief Trigger App Exposé.
 */
void inject_app_expose(void);

/**
 * @brief Trigger Show Desktop.
 */
void inject_show_desktop(void);

/**
 * @brief Toggle Launchpad.
 */
void inject_launchpad(void);

/**
 * @brief Switch to next macOS Space / Virtual Desktop.
 */
void inject_next_space(void);

/**
 * @brief Switch to previous macOS Space / Virtual Desktop.
 */
void inject_prev_space(void);

/**
 * @brief Synthesize a mouse click for given button.
 */
void inject_mouse_click(uint32_t button);

/**
 * @brief Synthesize smooth scrolling deltas.
 */
void inject_scroll(double dx, double dy);

/**
 * @brief Type an arbitrary text string.
 */
void inject_text(const char *text);

/**
 * @brief Dispatch and execute any action.
 */
void inject_action(const openlogi_action_t *action);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_INJECT_H */
