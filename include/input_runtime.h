#ifndef OPENLOGI_INPUT_RUNTIME_H
#define OPENLOGI_INPUT_RUNTIME_H
#include "config.h"
#include "hidpp_reprog.h"

#define INPUT_CONTROL_COUNT (CONFIG_BUTTON_GESTURE + 1)

typedef void (*input_dispatch_fn)(void *context, config_button_id_t button,
                                  gesture_direction_t direction, const openlogi_action_t *action);
typedef struct {
    bool down;
    bool captured;
    bool canceled;
    config_button_binding_t binding; /* Owns workflow copies until release/cancel. */
    swipe_accumulator_t gesture;
} input_press_t;
typedef struct {
    input_press_t presses[INPUT_CONTROL_COUNT];
    input_dispatch_fn dispatch;
    void *context;
} input_runtime_t;

/* Serialized by the caller. Configuration/binding pointers are borrowed only
 * during the call; press-time actions are copied and survive profile changes. */
void input_runtime_init(input_runtime_t *runtime, input_dispatch_fn dispatch, void *context);
void input_runtime_cancel(input_runtime_t *runtime);
bool input_runtime_button(input_runtime_t *runtime, config_button_id_t button, bool down,
                          const config_button_binding_t *binding);
bool input_runtime_captured(const input_runtime_t *runtime, config_button_id_t button);
void input_runtime_report(input_runtime_t *runtime, const hidpp_raw_report_t *report,
                          uint8_t feature, const openlogi_config_t *config,
                          const char *device_key, const char *app_id);
config_button_id_t input_button_for_cid(uint16_t cid);
bool input_binding_usable(const config_button_binding_t *binding);
#endif
