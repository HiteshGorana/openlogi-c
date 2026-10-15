#include "input_runtime.h"
#include "capture_plan.h"
#include <stdlib.h>
#include <string.h>

static bool action_usable(const openlogi_action_t *action)
{
    if (action->type <= ACTION_NONE || action->type > ACTION_WORKFLOW ||
        action->type == ACTION_SHOW_ACTIONS_RING) return false;
    /* Hold ownership is a later milestone. Preserve native input until then. */
    if (action->type == ACTION_KEY_COMBO && action->u.key_combo.hold) return false;
    return action->type != ACTION_WORKFLOW || (action->u.workflow &&
        action->u.workflow->step_count > 0 && action->u.workflow->step_count <= MAX_WORKFLOW_STEPS);
}

bool input_binding_usable(const config_button_binding_t *binding)
{
    if (!binding || !action_usable(&binding->action)) return false;
    const openlogi_action_t *directions[] = { &binding->gesture_up, &binding->gesture_down,
        &binding->gesture_left, &binding->gesture_right };
    if (binding->is_gesture) {
        for (size_t i = 0; i < 4; i++)
            if (directions[i]->type != ACTION_NONE && !action_usable(directions[i])) return false;
    }
    return true;
}

static void binding_free(config_button_binding_t *binding)
{
    openlogi_action_t *actions[] = { &binding->action, &binding->gesture_up, &binding->gesture_down,
        &binding->gesture_left, &binding->gesture_right };
    for (size_t i = 0; i < 5; i++) {
        if (actions[i]->type == ACTION_WORKFLOW) free(actions[i]->u.workflow);
    }
    memset(binding, 0, sizeof(*binding));
}

static bool binding_copy(config_button_binding_t *out, const config_button_binding_t *in)
{
    *out = *in;
    openlogi_action_t *dst[] = { &out->action, &out->gesture_up, &out->gesture_down,
        &out->gesture_left, &out->gesture_right };
    const openlogi_action_t *src[] = { &in->action, &in->gesture_up, &in->gesture_down,
        &in->gesture_left, &in->gesture_right };
    for (size_t i = 0; i < 5; i++) if (dst[i]->type == ACTION_WORKFLOW) dst[i]->u.workflow = NULL;
    for (size_t i = 0; i < 5; i++) {
        if (src[i]->type != ACTION_WORKFLOW || !src[i]->u.workflow) continue;
        dst[i]->u.workflow = malloc(sizeof(*dst[i]->u.workflow));
        if (!dst[i]->u.workflow) { binding_free(out); return false; }
        *dst[i]->u.workflow = *src[i]->u.workflow;
    }
    return true;
}

void input_runtime_init(input_runtime_t *runtime, input_dispatch_fn dispatch, void *context)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->dispatch = dispatch;
    runtime->context = context;
}

void input_runtime_cancel(input_runtime_t *runtime)
{
    for (size_t i = 0; i < INPUT_CONTROL_COUNT; i++) {
        binding_free(&runtime->presses[i].binding);
        runtime->presses[i].captured = false;
        runtime->presses[i].canceled = true;
        swipe_accumulator_init(&runtime->presses[i].gesture);
    }
}

bool input_runtime_captured(const input_runtime_t *runtime, config_button_id_t button)
{
    return button > CONFIG_BUTTON_UNKNOWN && button < INPUT_CONTROL_COUNT &&
        runtime->presses[button].down && runtime->presses[button].captured &&
        !runtime->presses[button].canceled;
}

static void dispatch(input_runtime_t *runtime, config_button_id_t button, gesture_direction_t direction)
{
    input_press_t *press = &runtime->presses[button];
    if (!press->captured || press->canceled) return;
    const openlogi_action_t *action = &press->binding.action;
    switch (direction) {
        case GESTURE_DIR_UP: action = &press->binding.gesture_up; break;
        case GESTURE_DIR_DOWN: action = &press->binding.gesture_down; break;
        case GESTURE_DIR_LEFT: action = &press->binding.gesture_left; break;
        case GESTURE_DIR_RIGHT: action = &press->binding.gesture_right; break;
        default: break;
    }
    if (action->type == ACTION_NONE) action = &press->binding.action;
    if (runtime->dispatch && action_usable(action)) runtime->dispatch(runtime->context, button, direction, action);
}

bool input_runtime_button(input_runtime_t *runtime, config_button_id_t button, bool down,
                          const config_button_binding_t *binding)
{
    if (button <= CONFIG_BUTTON_UNKNOWN || button >= INPUT_CONTROL_COUNT) return false;
    input_press_t *press = &runtime->presses[button];
    if (down == press->down) return press->captured && down;
    if (down) {
        press->down = true;
        press->canceled = false;
        press->captured = input_binding_usable(binding) && binding_copy(&press->binding, binding);
        swipe_accumulator_init(&press->gesture);
        if (press->captured && binding->is_gesture) swipe_accumulator_begin(&press->gesture);
        return press->captured;
    }
    bool captured = press->captured;
    if (captured && !press->canceled &&
        (!press->binding.is_gesture || swipe_accumulator_end(&press->gesture)))
        dispatch(runtime, button, GESTURE_DIR_CLICK);
    binding_free(&press->binding);
    memset(press, 0, sizeof(*press));
    return captured;
}

config_button_id_t input_button_for_cid(uint16_t cid)
{
    switch (cid) {
        case HIDPP_CID_GESTURE_BUTTON: case CID_GESTURE_BUTTON: return CONFIG_BUTTON_GESTURE;
        case HIDPP_CID_MODE_SHIFT_SMARTSHIFT: case HIDPP_CID_DPI_SWITCH_ED:
        case HIDPP_CID_DPI_SWITCH_FD: return CONFIG_BUTTON_DPI_TOGGLE;
        case HIDPP_CID_BACK_CLASSIC: case HIDPP_CID_BACK_MULTIPLAT:
        case HIDPP_CID_BACK_ALT: case HIDPP_CID_BACK_GENERIC: return CONFIG_BUTTON_BACK;
        case HIDPP_CID_FORWARD_CLASSIC: case HIDPP_CID_FORWARD_MULTIPLAT: return CONFIG_BUTTON_FORWARD;
        case CID_MIDDLE_BUTTON: return CONFIG_BUTTON_MIDDLE;
        default: return CONFIG_BUTTON_UNKNOWN;
    }
}

void input_runtime_report(input_runtime_t *runtime, const hidpp_raw_report_t *raw,
                          uint8_t feature, const openlogi_config_t *config,
                          const char *device_key, const char *app_id)
{
    hidpp_msg_t message;
    hidpp_reprog_event_t event;
    if (!raw || !feature || !hidpp_unpack(raw, &message) || message.proto != HIDPP_PROTO_V20 ||
        !hidpp_reprog_decode_event(&message.u.v20, feature, &event)) return;
    if (event.type == REPROG_EVENT_DIVERTED_BUTTONS) {
        bool down[INPUT_CONTROL_COUNT] = { false };
        for (size_t i = 0; i < event.u.buttons.count; i++) {
            config_button_id_t button = input_button_for_cid(event.u.buttons.pressed_cids[i]);
            if (button != CONFIG_BUTTON_UNKNOWN) down[button] = true;
        }
        for (int i = CONFIG_BUTTON_MIDDLE; i < INPUT_CONTROL_COUNT; i++) {
            const config_button_binding_t *binding = config_resolve_binding(config, device_key, app_id, (config_button_id_t)i);
            input_runtime_button(runtime, (config_button_id_t)i, down[i], binding);
        }
    } else if (event.type == REPROG_EVENT_RAW_XY) {
        for (int i = CONFIG_BUTTON_MIDDLE; i < INPUT_CONTROL_COUNT; i++) {
            input_press_t *press = &runtime->presses[i];
            if (!press->down || !press->captured || press->canceled || !press->binding.is_gesture) continue;
            gesture_direction_t direction = swipe_accumulator_accumulate(&press->gesture,
                event.u.raw_xy.dx, event.u.raw_xy.dy);
            if (direction != GESTURE_DIR_NONE) dispatch(runtime, (config_button_id_t)i, direction);
        }
    }
}
