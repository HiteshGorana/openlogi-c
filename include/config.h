/**
 * @file config.h
 * @brief Configuration model, TOML parser, and per-app profile resolver.
 */

#ifndef OPENLOGI_CONFIG_H
#define OPENLOGI_CONFIG_H

#include "inject.h"
#include "gesture.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENLOGI_SCHEMA_VERSION    7
#define OPENLOGI_MAX_DEVICES       8
#define OPENLOGI_MAX_APP_PROFILES  8
#define OPENLOGI_MAX_BINDINGS      12
#define OPENLOGI_MAX_DPI_PRESETS   8

typedef enum {
    CONFIG_BUTTON_UNKNOWN = 0,
    CONFIG_BUTTON_LEFT,
    CONFIG_BUTTON_RIGHT,
    CONFIG_BUTTON_MIDDLE,
    CONFIG_BUTTON_BACK,
    CONFIG_BUTTON_FORWARD,
    CONFIG_BUTTON_DPI_TOGGLE,
    CONFIG_BUTTON_GESTURE,
    CONFIG_BUTTON_THUMBWHEEL,
    CONFIG_BUTTON_WHEEL_TILT_LEFT,
    CONFIG_BUTTON_WHEEL_TILT_RIGHT,
    CONFIG_BUTTON_THUMBWHEEL_UP,
    CONFIG_BUTTON_THUMBWHEEL_DOWN,
    CONFIG_BUTTON_HAPTIC_PANEL
} config_button_id_t;

typedef struct {
    config_button_id_t button_id;
    bool is_gesture;              /* If true, uses gesture directions below */
    openlogi_action_t action;     /* Plain click / default action */
    openlogi_action_t gesture_up;
    openlogi_action_t gesture_down;
    openlogi_action_t gesture_left;
    openlogi_action_t gesture_right;
} config_button_binding_t;

typedef struct {
    char app_id[128];             /* e.g. "com.apple.Safari", or empty for default */
    config_button_binding_t bindings[OPENLOGI_MAX_BINDINGS];
    size_t binding_count;
} config_app_profile_t;

typedef struct {
    char device_key[128];         /* e.g. "unit:6be9d300", "receiver:aabb:slot:1", or "default" */
    char custom_name[64];
    bool enabled;
    uint16_t dpi;
    uint16_t dpi_presets[OPENLOGI_MAX_DPI_PRESETS];
    size_t dpi_preset_count;

    uint8_t smartshift_mode;      /* 0=disabled, 1=ratchet, 2=freespin, 3=auto */
    uint8_t smartshift_threshold; /* 1..255 */
    bool invert_scroll;           /* Invert mouse wheel scroll direction */

    config_app_profile_t default_profile;
    config_app_profile_t app_profiles[OPENLOGI_MAX_APP_PROFILES];
    size_t app_profile_count;
} config_device_t;

typedef struct {
    uint32_t schema_version;
    char selected_device[128];
    config_device_t devices[OPENLOGI_MAX_DEVICES];
    size_t device_count;
} openlogi_config_t;

/* --- Path & Parsing APIs --- */

/**
 * @brief Get the standard OpenLogi configuration path (~/.config/openlogi/config.toml).
 */
bool config_get_default_path(char *out_path, size_t max_len);

/**
 * @brief Parse a configuration from TOML string.
 */
bool config_parse_string(const char *toml_str, openlogi_config_t *out_config, char *errbuf, size_t errbufsz);

/**
 * @brief Load configuration from file path.
 */
bool config_load_file(const char *filepath, openlogi_config_t *out_config, char *errbuf, size_t errbufsz);

/**
 * @brief Free all heap-allocated resources in a configuration (e.g. workflows).
 */
void config_free(openlogi_config_t *cfg);

/**
 * @brief Helper to resolve button action for a given device, foreground application, and button ID.
 */
const config_device_t *config_resolve_device(const openlogi_config_t *cfg, const char *device_key);

const config_button_binding_t *config_resolve_binding(
    const openlogi_config_t *cfg,
    const char *device_key,
    const char *current_app_id,
    config_button_id_t button_id
);

/**
 * @brief Convert string action name to action structure.
 */
bool action_from_string(const char *str, openlogi_action_t *out_action);

/**
 * @brief Convert button string to button ID.
 */
config_button_id_t button_id_from_string(const char *str);

/**
 * @brief Convert button ID to canonical button name string.
 */
const char *button_id_to_string(config_button_id_t btn);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_CONFIG_H */
