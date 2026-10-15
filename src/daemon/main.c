/**
 * @file main.c
 * @brief OpenLogi Pure C Daemon (openlogid) entry point.
 */

#include "signals.h"
#include "config.h"
#include "config_watch.h"
#include "app_watch.h"
#include "hook.h"
#include "gesture.h"
#include "inject.h"
#include "ipc_server.h"
#include "device.h"
#include "hidpp_reprog.h"
#include "single_instance.h"
#include "subcommands.h"
#include "button_runtime.h"
#include "capture_plan.h"
#include "action_queue.h"
#include "scroll_pipeline.h"
#include "armed_session.h"
#include "device_reapply.h"
#include "hires_wheel.h"
#include "daemon_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOHIDLib.h>
extern bool CGPreflightListenEventAccess(void);
extern bool CGRequestListenEventAccess(void);
#endif

static openlogi_daemon_ctx_t g_daemon_ctx;
static char g_config_file[512] = { 0 };
static char g_socket_file[512] = { 0 };
static char g_lock_file[512] = { 0 };
static char g_session_file[512] = { 0 };
static bool g_verbose = false;
static button_runtime_t g_button_rt;
static volatile bool g_pending_hotplug_probe = false;
static volatile bool g_pending_reconcile = false;

static uint64_t daemon_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static bool daemon_reload_config(openlogi_daemon_ctx_t *ctx);
static void daemon_probe_hardware(openlogi_daemon_ctx_t *ctx);
static size_t count_online_usable_devices(const openlogi_daemon_ctx_t *ctx);
static void daemon_apply_diversion(openlogi_daemon_ctx_t *ctx);
static void daemon_undivert_all(void);
static void daemon_reconcile_runtime_state(openlogi_daemon_ctx_t *ctx);

static void on_config_reloaded(const char *filepath, void *user_data)
{
    (void)user_data;
    printf("[openlogid] Configuration file modified (%s). Hot-reloading...\n", filepath);
    if (daemon_reload_config(&g_daemon_ctx)) {
        printf("[openlogid] Configuration reloaded successfully (schema version %u).\n",
               g_daemon_ctx.config.schema_version);
        ipc_server_broadcast_event("config_reloaded", "{\"success\":true}");
    }
}

static void on_app_changed(const char *app_id, void *user_data)
{
    (void)user_data;
    if (g_verbose) {
        printf("[openlogid] Active foreground app: %s\n", app_id);
    }
    button_runtime_cancel_all(&g_button_rt, BUTTON_END_CANCELED_INVALIDATED);
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"app_id\":\"%s\"}", app_id);
    ipc_server_broadcast_event("app_activated", payload);
}

static swipe_accumulator_t g_gesture_acc;
static config_button_id_t g_active_gesture_btn = CONFIG_BUTTON_UNKNOWN;

/* Forward declarations */
static bool daemon_set_dpi(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint16_t dpi);
static bool daemon_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t threshold);

static void daemon_dispatch_action(openlogi_daemon_ctx_t *ctx, const openlogi_action_t *action, const char *source_desc)
{
    if (!action || action->type == ACTION_NONE) return;

    if (g_verbose) {
        printf("[openlogid] Action %d triggered via %s\n", (int)action->type, source_desc ? source_desc : "unknown");
    }

    if (action->type == ACTION_TOGGLE_SMARTSHIFT) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            uint8_t cur = ctx->devices[i].smartshift_mode;
            uint8_t next = (cur == 1) ? 2 : 1;
            daemon_set_smartshift(ctx, ctx->devices[i].device_key, next, ctx->devices[i].smartshift_threshold);
        }
        ipc_server_broadcast_event("smartshift_changed", "{\"toggled\":true}");
        return;
    }

    if (action->type == ACTION_CYCLE_DPI) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            uint16_t cur_dpi = ctx->devices[i].dpi;
            uint16_t next_dpi = (cur_dpi < 1600) ? 1600 : (cur_dpi < 2400 ? 2400 : 1000);
            if (ctx->config.device_count > 0 && ctx->config.devices[0].dpi_preset_count > 0) {
                const config_device_t *cdev = &ctx->config.devices[0];
                for (size_t p = 0; p < cdev->dpi_preset_count; p++) {
                    if (cdev->dpi_presets[p] == cur_dpi) {
                        next_dpi = cdev->dpi_presets[(p + 1) % cdev->dpi_preset_count];
                        break;
                    }
                }
            }
            daemon_set_dpi(ctx, ctx->devices[i].device_key, next_dpi);
        }
        ipc_server_broadcast_event("dpi_changed", "{\"cycled\":true}");
        return;
    }

    if (action->type == ACTION_SET_DPI_PRESET) {
        uint8_t p_idx = action->u.dpi_preset_index;
        if (ctx->config.device_count > 0 && p_idx < ctx->config.devices[0].dpi_preset_count) {
            uint16_t target = ctx->config.devices[0].dpi_presets[p_idx];
            for (size_t i = 0; i < ctx->device_count; i++) {
                daemon_set_dpi(ctx, ctx->devices[i].device_key, target);
            }
        }
        return;
    }

    if (action_queue_is_blocking(action)) {
        if (g_verbose) {
            printf("[openlogid] Offloading blocking action to async worker\n");
        }
        action_queue_enqueue(action);
    } else {
        inject_action(action);
    }
}

static void on_button_runtime_event(const button_runtime_event_t *ev, void *user_data)
{
    openlogi_daemon_ctx_t *ctx = (openlogi_daemon_ctx_t *)user_data;
    if (!ctx || !ev) return;

    if (ev->type == BUTTON_EVT_TRIGGERED) {
        config_button_id_t btn_id = (config_button_id_t)ev->press.button_id;
        char front_app[256] = { 0 };
        app_watch_get_cached(front_app, sizeof(front_app));

        const config_button_binding_t *binding = config_resolve_binding(
            &ctx->config, NULL, front_app, btn_id
        );

        if (!binding) return;

        const openlogi_action_t *to_run = &binding->action;
        if (to_run && to_run->type != ACTION_NONE) {
            char desc[128];
            snprintf(desc, sizeof(desc), "Button %s [%s] (app: %s)",
                     button_id_to_string(btn_id),
                     ev->action_type == TRIGGER_ACTION_LONG_PRESS ? "LongPress" : "Click",
                     front_app);
            daemon_dispatch_action(ctx, to_run, desc);
        }
    }
}

static void daemon_dispatch_gesture(openlogi_daemon_ctx_t *ctx, config_button_id_t btn_id, gesture_direction_t dir)
{
    char front_app[256] = { 0 };
    app_watch_get_cached(front_app, sizeof(front_app));

    const config_button_binding_t *binding = config_resolve_binding(
        &ctx->config, NULL, front_app, btn_id
    );

    if (!binding) return;

    const openlogi_action_t *to_run = NULL;
    const char *dir_name = gesture_direction_name(dir);

    switch (dir) {
        case GESTURE_DIR_UP:
            to_run = (binding->gesture_up.type != ACTION_NONE) ? &binding->gesture_up : &binding->action;
            break;
        case GESTURE_DIR_DOWN:
            to_run = (binding->gesture_down.type != ACTION_NONE) ? &binding->gesture_down : &binding->action;
            break;
        case GESTURE_DIR_LEFT:
            to_run = (binding->gesture_left.type != ACTION_NONE) ? &binding->gesture_left : &binding->action;
            break;
        case GESTURE_DIR_RIGHT:
            to_run = (binding->gesture_right.type != ACTION_NONE) ? &binding->gesture_right : &binding->action;
            break;
        case GESTURE_DIR_CLICK:
        default:
            to_run = &binding->action;
            break;
    }

    if (to_run && to_run->type != ACTION_NONE) {
        char desc[128];
        snprintf(desc, sizeof(desc), "Gesture (%s, dir=%s, app=%s)",
                 (btn_id == CONFIG_BUTTON_GESTURE ? "ThumbButton" : "DpiToggle"),
                 dir_name, front_app);
        daemon_dispatch_action(ctx, to_run, desc);

        char payload[256];
        snprintf(payload, sizeof(payload), "{\"button\":\"%s\",\"direction\":\"%s\",\"action\":%d}",
                 (btn_id == CONFIG_BUTTON_GESTURE ? "GestureButton" : "DpiToggle"),
                 dir_name, (int)to_run->type);
        ipc_server_broadcast_event("gesture_triggered", payload);
    }
}

static const config_device_t *daemon_get_active_device_config(const openlogi_daemon_ctx_t *ctx)
{
    if (!ctx || ctx->config.device_count == 0) return NULL;
    const char *sel = ctx->config.selected_device;
    if (sel[0] != '\0') {
        for (size_t i = 0; i < ctx->config.device_count; i++) {
            if (strcmp(ctx->config.devices[i].device_key, sel) == 0) {
                return &ctx->config.devices[i];
            }
        }
    }
    for (size_t i = 0; i < ctx->config.device_count; i++) {
        if (strcmp(ctx->config.devices[i].device_key, "default") == 0) {
            return &ctx->config.devices[i];
        }
    }
    return &ctx->config.devices[0];
}

static hook_action_t on_hook_event(const hook_event_t *event, void *user_data)
{
    (void)user_data;
    if (!event) return HOOK_ACTION_PASS;

    /* Handle scroll events with in-place inversion */
    if (event->type == HOOK_EVENT_SCROLL) {
        bool invert = false;
        const config_device_t *cdev = daemon_get_active_device_config(&g_daemon_ctx);
        if (cdev) {
            invert = cdev->invert_scroll;
        }
        return invert ? HOOK_ACTION_INVERT : HOOK_ACTION_PASS;
    }

    /* Map intercepted OS mouse event to button identifier */
    config_button_id_t btn_id = CONFIG_BUTTON_UNKNOWN;
    if (event->type == HOOK_EVENT_BUTTON_DOWN || event->type == HOOK_EVENT_BUTTON_UP) {
        if (event->button_number == 3) btn_id = CONFIG_BUTTON_MIDDLE;
        else if (event->button_number == 4) btn_id = CONFIG_BUTTON_BACK;
        else if (event->button_number == 5) btn_id = CONFIG_BUTTON_FORWARD;
        else if (event->button_number == 6) btn_id = CONFIG_BUTTON_GESTURE;
        else if (event->button_number == 7) btn_id = CONFIG_BUTTON_DPI_TOGGLE;
        else if (event->button_number == 1) btn_id = CONFIG_BUTTON_LEFT;
        else if (event->button_number == 2) btn_id = CONFIG_BUTTON_RIGHT;
    }

    if (btn_id != CONFIG_BUTTON_UNKNOWN) {
        char front_app[256] = { 0 };
        app_watch_get_cached(front_app, sizeof(front_app));

        const config_button_binding_t *binding = config_resolve_binding(
            &g_daemon_ctx.config, NULL, front_app, btn_id
        );

        if (binding && binding->action.type != ACTION_NONE) {
            uint64_t now_ms = daemon_get_time_ms();
            if (event->type == HOOK_EVENT_BUTTON_DOWN) {
                button_runtime_down(&g_button_rt, (uint32_t)btn_id, false, now_ms, NULL);
                return HOOK_ACTION_SUPPRESS;
            } else if (event->type == HOOK_EVENT_BUTTON_UP) {
                if (button_runtime_is_pressed(&g_button_rt, (uint32_t)btn_id)) {
                    button_runtime_up(&g_button_rt, (uint32_t)btn_id, now_ms);
                    return HOOK_ACTION_SUPPRESS;
                }
            }
        } else if (event->type == HOOK_EVENT_BUTTON_UP) {
            if (button_runtime_is_pressed(&g_button_rt, (uint32_t)btn_id)) {
                uint64_t now_ms = daemon_get_time_ms();
                button_runtime_up(&g_button_rt, (uint32_t)btn_id, now_ms);
                return HOOK_ACTION_SUPPRESS;
            }
        }
    }

    return HOOK_ACTION_PASS;
}

typedef struct {
    hid_device_t *dev_handle;
    hid_device_t *secondary_handle;
    uint8_t device_index;
    uint8_t sw_id;
    uint8_t feat_dpi;
    uint8_t feat_smartshift;
    uint8_t feat_reprog;
    uint8_t feat_hires_wheel;
} live_device_handle_t;

static live_device_handle_t g_live_handles[OPENLOGI_MAX_DEVICES];

static void on_hid_report_event(hid_device_t *dev, const hidpp_raw_report_t *raw, void *user_data)
{
    openlogi_daemon_ctx_t *ctx = (openlogi_daemon_ctx_t *)user_data;
    if (!dev || !raw || !ctx) return;

    /* Find matching device handle info to get feature_index for reprog */
    uint8_t feat_reprog = 0;
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (g_live_handles[i].dev_handle == dev || g_live_handles[i].secondary_handle == dev) {
            feat_reprog = g_live_handles[i].feat_reprog;
            break;
        }
    }
    if (feat_reprog == 0) return;

    hidpp_msg_t msg;
    if (!hidpp_unpack(raw, &msg)) return;
    if (msg.proto != HIDPP_PROTO_V20) return;
    if (msg.u.v20.feature_index != feat_reprog) return;

    hidpp_reprog_event_t ev;
    if (!hidpp_reprog_decode_event(&msg.u.v20, feat_reprog, &ev)) return;

    if (ev.type == REPROG_EVENT_DIVERTED_BUTTONS) {
        bool thumb_down = false;
        bool dpi_toggle_down = false;

        for (uint8_t c = 0; c < ev.u.buttons.count; c++) {
            uint16_t cid = ev.u.buttons.pressed_cids[c];
            if (cid == HIDPP_CID_GESTURE_BUTTON || cid == CID_GESTURE_BUTTON) {
                thumb_down = true;
            } else if (cid == HIDPP_CID_MODE_SHIFT_SMARTSHIFT ||
                       cid == HIDPP_CID_DPI_SWITCH_ED ||
                       cid == HIDPP_CID_DPI_SWITCH_FD) {
                dpi_toggle_down = true;
            } else if (cid == CID_BACK_BUTTON || cid == HIDPP_CID_BACK_CLASSIC || cid == HIDPP_CID_BACK_MULTIPLAT) {
                button_runtime_down(&g_button_rt, (uint32_t)CONFIG_BUTTON_BACK, false, daemon_get_time_ms(), NULL);
            } else if (cid == CID_FORWARD_BUTTON || cid == HIDPP_CID_FORWARD_CLASSIC || cid == HIDPP_CID_FORWARD_MULTIPLAT) {
                button_runtime_down(&g_button_rt, (uint32_t)CONFIG_BUTTON_FORWARD, false, daemon_get_time_ms(), NULL);
            } else if (cid == CID_MIDDLE_BUTTON) {
                button_runtime_down(&g_button_rt, (uint32_t)CONFIG_BUTTON_MIDDLE, false, daemon_get_time_ms(), NULL);
            }
        }

        if (thumb_down) {
            if (!swipe_accumulator_is_holding(&g_gesture_acc)) {
                if (g_verbose) {
                    printf("[openlogid] Diverted Thumb Gesture Button DOWN\n");
                }
                g_active_gesture_btn = CONFIG_BUTTON_GESTURE;
                swipe_accumulator_begin(&g_gesture_acc);
            }
        } else if (dpi_toggle_down) {
            if (!swipe_accumulator_is_holding(&g_gesture_acc)) {
                if (g_verbose) {
                    printf("[openlogid] Diverted ModeShift/DPI Button DOWN\n");
                }
                g_active_gesture_btn = CONFIG_BUTTON_DPI_TOGGLE;
                swipe_accumulator_begin(&g_gesture_acc);
            }
        } else {
            /* All diverted buttons released */
            if (swipe_accumulator_is_holding(&g_gesture_acc)) {
                config_button_id_t btn = g_active_gesture_btn;
                bool was_click = swipe_accumulator_end(&g_gesture_acc);
                g_active_gesture_btn = CONFIG_BUTTON_UNKNOWN;
                if (g_verbose) {
                    printf("[openlogid] Diverted Button UP (plain_click=%d)\n", (int)was_click);
                }
                if (was_click) {
                    daemon_dispatch_gesture(ctx, btn, GESTURE_DIR_CLICK);
                }
            }
            uint64_t now_ms = daemon_get_time_ms();
            if (button_runtime_is_pressed(&g_button_rt, (uint32_t)CONFIG_BUTTON_BACK)) {
                button_runtime_up(&g_button_rt, (uint32_t)CONFIG_BUTTON_BACK, now_ms);
            }
            if (button_runtime_is_pressed(&g_button_rt, (uint32_t)CONFIG_BUTTON_FORWARD)) {
                button_runtime_up(&g_button_rt, (uint32_t)CONFIG_BUTTON_FORWARD, now_ms);
            }
            if (button_runtime_is_pressed(&g_button_rt, (uint32_t)CONFIG_BUTTON_MIDDLE)) {
                button_runtime_up(&g_button_rt, (uint32_t)CONFIG_BUTTON_MIDDLE, now_ms);
            }
        }
    } else if (ev.type == REPROG_EVENT_RAW_XY) {
        if (swipe_accumulator_is_holding(&g_gesture_acc)) {
            gesture_direction_t committed = swipe_accumulator_accumulate(
                &g_gesture_acc,
                ev.u.raw_xy.dx,
                ev.u.raw_xy.dy
            );
            if (committed != GESTURE_DIR_NONE) {
                if (g_verbose) {
                    printf("[openlogid] Gesture swipe committed: %s\n", gesture_direction_name(committed));
                }
                daemon_dispatch_gesture(ctx, g_active_gesture_btn, committed);
            }
        }
    }
}

static size_t count_online_usable_devices(const openlogi_daemon_ctx_t *ctx)
{
    if (!ctx) return 0;
    size_t count = 0;
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (ctx->devices[i].online && g_live_handles[i].dev_handle != NULL) {
            count++;
        }
    }
    return count;
}

static void daemon_apply_diversion(openlogi_daemon_ctx_t *ctx)
{
    if (!ctx || !hook_is_running()) return;

    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!ctx->devices[i].online || !g_live_handles[i].dev_handle) continue;
        if (g_live_handles[i].feat_reprog == 0) continue;

        const config_device_t *cdev = NULL;
        for (size_t cd = 0; cd < ctx->config.device_count; cd++) {
            if (strcmp(ctx->config.devices[cd].device_key, ctx->devices[i].device_key) == 0 ||
                strcmp(ctx->config.devices[cd].device_key, "default") == 0) {
                cdev = &ctx->config.devices[cd];
                break;
            }
        }
        if (!cdev && ctx->config.device_count > 0) {
            cdev = &ctx->config.devices[0];
        }

        capture_spec_t plan;
        capture_plan_resolve(cdev, &plan);
        if (plan.divert_count == 0) {
            capture_spec_add_cid(&plan, HIDPP_CID_GESTURE_BUTTON);
            capture_spec_add_cid(&plan, HIDPP_CID_MODE_SHIFT_SMARTSHIFT);
            plan.wants_raw_xy = true;
        }

        for (size_t c = 0; c < plan.divert_count; c++) {
            uint16_t cid = plan.divert_cids[c];
            bool raw_xy = (cid == CID_GESTURE_BUTTON || cid == HIDPP_CID_GESTURE_BUTTON) ? plan.wants_raw_xy : false;
            hidpp_reprog_set_reporting(
                g_live_handles[i].dev_handle,
                g_live_handles[i].device_index,
                g_live_handles[i].feat_reprog,
                g_live_handles[i].sw_id,
                cid,
                true,
                raw_xy
            );
        }

        hid_register_report_callback(g_live_handles[i].dev_handle, on_hid_report_event, ctx);

        if (g_live_handles[i].secondary_handle) {
            hidpp_reprog_set_reporting(
                g_live_handles[i].secondary_handle,
                g_live_handles[i].device_index,
                g_live_handles[i].feat_reprog,
                g_live_handles[i].sw_id,
                HIDPP_CID_GESTURE_BUTTON,
                true,
                true
            );
            hidpp_reprog_set_reporting(
                g_live_handles[i].secondary_handle,
                g_live_handles[i].device_index,
                g_live_handles[i].feat_reprog,
                g_live_handles[i].sw_id,
                HIDPP_CID_MODE_SHIFT_SMARTSHIFT,
                true,
                true
            );
            hid_register_report_callback(g_live_handles[i].secondary_handle, on_hid_report_event, ctx);
        }
    }
}

static void daemon_undivert_all(void)
{
    hidpp_diversion_teardown_all();
}

static void daemon_reconcile_runtime_state(openlogi_daemon_ctx_t *ctx)
{
    if (!ctx) return;

    size_t usable = count_online_usable_devices(ctx);
    openlogi_runtime_state_t target = daemon_compute_target_state(usable);

    if (!daemon_state_should_transition(ctx->runtime_state, target)) {
        return; /* Idempotent: state already reconciled */
    }

    if (target == RUNTIME_STATE_ACTIVE) {
        printf("[openlogid] Usable Logitech device online (%zu device%s); activating input handling...\n",
               usable, usable > 1 ? "s" : "");

        /* 1. Start foreground application profile watcher */
        if (!app_watch_is_running()) {
            app_watch_start(on_app_changed, NULL);
        }

        /* 2. Start OS event tap hook */
        bool hook_ok = false;
        if (!hook_is_running()) {
            hook_ok = hook_init(on_hook_event, NULL) && hook_start();
            if (hook_ok) {
                printf("[openlogid] OS input interception hook active.\n");
            } else {
                printf("[openlogid] Notice: Accessibility/Input Monitoring permission not yet granted; hook in passive mode.\n");
            }
        } else {
            hook_ok = true;
        }

        /* 3. Apply hardware diversion only if hook is functional */
        if (hook_ok) {
            daemon_apply_diversion(ctx);
            ctx->runtime_state = RUNTIME_STATE_ACTIVE;
            ipc_server_broadcast_event("runtime_state_changed", "{\"state\":\"active\"}");
        } else {
            ctx->runtime_state = RUNTIME_STATE_STANDBY;
            printf("[openlogid] Input hook not active (missing permissions); retaining lightweight standby.\n");
        }
    } else {
        printf("[openlogid] No usable devices online; entering lightweight standby...\n");

        /* 1. Invalidate device runtime state */
        button_runtime_cancel_all(&g_button_rt, BUTTON_END_CANCELED_INVALIDATED);
        swipe_accumulator_init(&g_gesture_acc);
        g_active_gesture_btn = CONFIG_BUTTON_UNKNOWN;
        action_queue_clear_pending();

        /* 2. Disarm any accessible hardware diversion */
        daemon_undivert_all();

        /* 3. Stop OS input hook */
        if (hook_is_running()) {
            hook_stop();
        }

        /* 4. Stop foreground application profile watcher */
        if (app_watch_is_running()) {
            app_watch_stop();
        }

        ctx->runtime_state = RUNTIME_STATE_STANDBY;
        ipc_server_broadcast_event("runtime_state_changed", "{\"state\":\"standby\"}");
    }
}

static void on_hotplug_event(const hid_device_info_t *info, bool connected, void *user_data);

static void daemon_probe_hardware(openlogi_daemon_ctx_t *ctx)
{
    int init_res = hid_transport_init();
    if (init_res != 0) {
        if (g_verbose) {
            printf("[openlogid] Notice: hid_transport_init returned %d\n", init_res);
        }
        return;
    }

    hid_device_info_t dev_infos[16];
    int count = hid_enumerate(dev_infos, 16);
    if (count <= 0) {
        if (g_verbose) {
            printf("[openlogid] No Logitech HID++ devices discovered during hardware scan.\n");
        }
        return;
    }

    if (g_verbose) {
        printf("[openlogid] Discovered %d Logitech HID collection(s). Probing...\n", count);
    }

    /* Snapshot for re-apply comparison */
    reapply_device_t prev_devs[OPENLOGI_MAX_DEVICES];
    size_t prev_count = ctx->device_count;
    for (size_t d = 0; d < prev_count; d++) {
        memset(&prev_devs[d], 0, sizeof(prev_devs[d]));
        strncpy(prev_devs[d].device_key, ctx->devices[d].device_key, sizeof(prev_devs[d].device_key) - 1);
        prev_devs[d].slot = (uint8_t)d;
        prev_devs[d].online = ctx->devices[d].online;
    }

    ctx->device_count = 0;

    for (int i = 0; i < count && ctx->device_count < OPENLOGI_MAX_DEVICES; i++) {
        const hid_device_info_t *info = &dev_infos[i];

        /* Deduplicate if serial already probed: register secondary collection */
        if (info->serial_number[0] != '\0') {
            bool already = false;
            for (size_t d = 0; d < ctx->device_count; d++) {
                if (strcmp(ctx->devices[d].serial_number, info->serial_number) == 0) {
                    already = true;
                    if (!g_live_handles[d].secondary_handle) {
                        hid_device_t *sec_dev = hid_open(info->transport_path);
                        if (sec_dev) {
                            g_live_handles[d].secondary_handle = sec_dev;
                        }
                    }
                    break;
                }
            }
            if (already) continue;
        }

        hid_device_t *dev = hid_open(info->transport_path);
        if (!dev) continue;

        openlogi_device_t probed;
        if (openlogi_probe_device(dev, HIDPP_DEVICE_RECEIVER, HIDPP_DEFAULT_SW_ID, &probed)) {
            size_t idx = ctx->device_count;
            openlogi_device_status_t *st = &ctx->devices[idx];
            memset(st, 0, sizeof(*st));

            if (info->serial_number[0] != '\0') {
                snprintf(st->device_key, sizeof(st->device_key), "device:%s", info->serial_number);
                strncpy(st->serial_number, info->serial_number, sizeof(st->serial_number) - 1);
            } else {
                snprintf(st->device_key, sizeof(st->device_key), "dev:0x%04x:0x%04x", info->vendor_id, info->product_id);
            }

            strncpy(st->product_name, probed.name[0] ? probed.name : info->product_name, sizeof(st->product_name) - 1);
            st->vendor_id = info->vendor_id;
            st->product_id = info->product_id;
            st->battery_percent = probed.battery_percent;
            st->is_charging = probed.battery_charging;
            st->dpi = probed.current_dpi;
            st->smartshift_mode = (uint8_t)probed.smartshift_mode;
            st->smartshift_threshold = probed.smartshift_threshold;
            st->online = true;

            g_live_handles[idx].dev_handle = dev;
            g_live_handles[idx].secondary_handle = NULL;
            g_live_handles[idx].device_index = HIDPP_DEVICE_RECEIVER;
            g_live_handles[idx].sw_id = HIDPP_DEFAULT_SW_ID;
            g_live_handles[idx].feat_dpi = probed.feat_dpi;
            g_live_handles[idx].feat_smartshift = probed.feat_smartshift;
            g_live_handles[idx].feat_reprog = probed.feat_reprog_controls;
            g_live_handles[idx].feat_hires_wheel = probed.feat_hires_wheel;

            const config_device_t *cdev = NULL;
            for (size_t cd = 0; cd < ctx->config.device_count; cd++) {
                if (strcmp(ctx->config.devices[cd].device_key, st->device_key) == 0 ||
                    strcmp(ctx->config.devices[cd].device_key, "default") == 0) {
                    cdev = &ctx->config.devices[cd];
                    break;
                }
            }
            if (!cdev && ctx->config.device_count > 0) {
                cdev = &ctx->config.devices[0];
            }


            /* Configure hardware hi-res wheel inversion if Feature 0x2121 is supported */
            if (probed.feat_hires_wheel != 0) {
                hidpp_v20_msg_t hw_req;
                hires_wheel_build_mode_request(
                    probed.feat_hires_wheel,
                    HIDPP_DEFAULT_SW_ID,
                    cdev ? cdev->invert_scroll : false,
                    false,
                    &hw_req
                );
                hidpp_msg_t hw_resp;
                hid_transact(dev, &hw_req, &hw_resp, 1000);
            }

            ctx->device_count++;

            printf("[openlogid] Discovered hardware: %s (DPI: %u, Battery: %u%%, SmartShift: %u)\n",
                   st->product_name, st->dpi, st->battery_percent, st->smartshift_mode);
        } else {
            hid_close(dev);
        }
    }

    /* Compute re-apply plan for new arrivals / reconnected hardware */
    reapply_device_t next_devs[OPENLOGI_MAX_DEVICES];
    for (size_t d = 0; d < ctx->device_count; d++) {
        memset(&next_devs[d], 0, sizeof(next_devs[d]));
        strncpy(next_devs[d].device_key, ctx->devices[d].device_key, sizeof(next_devs[d].device_key) - 1);
        next_devs[d].slot = (uint8_t)d;
        next_devs[d].online = ctx->devices[d].online;
    }

    reapply_plan_t plan;
    device_reapply_plan(prev_devs, prev_count, next_devs, ctx->device_count, false, &plan);

    for (size_t p = 0; p < plan.target_count; p++) {
        size_t idx = plan.target_indices[p];
        if (idx < ctx->device_count && ctx->devices[idx].online) {
            const config_device_t *cdev = NULL;
            for (size_t cd = 0; cd < ctx->config.device_count; cd++) {
                if (strcmp(ctx->config.devices[cd].device_key, ctx->devices[idx].device_key) == 0 ||
                    strcmp(ctx->config.devices[cd].device_key, "default") == 0) {
                    cdev = &ctx->config.devices[cd];
                    break;
                }
            }
            if (!cdev && ctx->config.device_count > 0) {
                cdev = &ctx->config.devices[0];
            }
            if (cdev) {
                if (cdev->dpi > 0 && ctx->devices[idx].dpi != cdev->dpi) {
                    daemon_set_dpi(ctx, ctx->devices[idx].device_key, cdev->dpi);
                }
                if (cdev->smartshift_threshold > 0) {
                    daemon_set_smartshift(ctx, ctx->devices[idx].device_key, cdev->smartshift_mode, cdev->smartshift_threshold);
                }
            }
        }
    }

    if (ctx->runtime_state == RUNTIME_STATE_ACTIVE && hook_is_running()) {
        daemon_apply_diversion(ctx);
    }
}

static void on_hotplug_event(const hid_device_info_t *info, bool connected, void *user_data)
{
    openlogi_daemon_ctx_t *ctx = (openlogi_daemon_ctx_t *)user_data;
    if (!ctx || !info) return;

    if (!connected) {
        if (g_verbose) {
            printf("[openlogid] Hotplug: device disconnected: %s (%s)\n",
                   info->product_name[0] ? info->product_name : "Logitech device",
                   info->transport_path);
        }
        for (size_t i = 0; i < ctx->device_count; i++) {
            bool match = false;
            if (info->serial_number[0] != '\0' && ctx->devices[i].serial_number[0] != '\0') {
                match = (strcmp(ctx->devices[i].serial_number, info->serial_number) == 0);
            } else if (ctx->devices[i].vendor_id == info->vendor_id &&
                       ctx->devices[i].product_id == info->product_id) {
                match = true;
            }

            if (match) {
                ctx->devices[i].online = false;
                if (g_live_handles[i].dev_handle) {
                    hidpp_diversion_untrack_device(g_live_handles[i].dev_handle);
                    g_live_handles[i].dev_handle = NULL;
                }
                if (g_live_handles[i].secondary_handle) {
                    hidpp_diversion_untrack_device(g_live_handles[i].secondary_handle);
                    g_live_handles[i].secondary_handle = NULL;
                }
                /* Per-device disconnect cleanup */
                button_runtime_cancel_all(&g_button_rt, BUTTON_END_CANCELED_INVALIDATED);
                swipe_accumulator_init(&g_gesture_acc);
                g_active_gesture_btn = CONFIG_BUTTON_UNKNOWN;
                action_queue_clear_pending();

                char payload[128];
                snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"online\":false}", ctx->devices[i].device_key);
                ipc_server_broadcast_event("device_disconnected", payload);
            }
        }
        g_pending_reconcile = true;
    } else {
        if (g_verbose) {
            printf("[openlogid] Hotplug: device connected: %s (%s)\n",
                   info->product_name[0] ? info->product_name : "Logitech device",
                   info->transport_path);
        }
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"product\":\"%s\",\"online\":true}", info->product_name);
        ipc_server_broadcast_event("device_connected", payload);
        g_pending_hotplug_probe = true;
    }
}

static bool daemon_set_dpi(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint16_t dpi)
{
    if (g_verbose) {
        printf("[openlogid] Setting DPI to %u for device %s\n", dpi, dev_key ? dev_key : "default");
    }

    bool matched = false;
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            matched = true;
            if (g_live_handles[i].dev_handle && g_live_handles[i].feat_dpi != 0) {
                bool ok = hidpp_write_dpi(g_live_handles[i].dev_handle,
                                          g_live_handles[i].device_index,
                                          g_live_handles[i].feat_dpi,
                                          g_live_handles[i].sw_id,
                                          dpi);
                if (ok) {
                    ctx->devices[i].dpi = dpi;
                    return true;
                } else {
                    if (g_verbose) {
                        fprintf(stderr, "[openlogid] Failed to write DPI to device %s\n", ctx->devices[i].device_key);
                    }
                    return false;
                }
            } else {
                ctx->devices[i].dpi = dpi;
                return true;
            }
        }
    }
    return matched;
}

static bool daemon_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t threshold)
{
    if (g_verbose) {
        printf("[openlogid] Setting SmartShift mode=%u thresh=%u for device %s\n",
               mode, threshold, dev_key ? dev_key : "default");
    }

    bool matched = false;
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            matched = true;
            if (g_live_handles[i].dev_handle && g_live_handles[i].feat_smartshift != 0) {
                bool ok = hidpp_write_smartshift(g_live_handles[i].dev_handle,
                                                 g_live_handles[i].device_index,
                                                 g_live_handles[i].feat_smartshift,
                                                 g_live_handles[i].sw_id,
                                                 (smartshift_mode_t)mode,
                                                 threshold);
                if (ok) {
                    ctx->devices[i].smartshift_mode = mode;
                    ctx->devices[i].smartshift_threshold = threshold;
                    return true;
                } else {
                    if (g_verbose) {
                        fprintf(stderr, "[openlogid] Failed to write SmartShift to device %s\n", ctx->devices[i].device_key);
                    }
                    return false;
                }
            } else {
                ctx->devices[i].smartshift_mode = mode;
                ctx->devices[i].smartshift_threshold = threshold;
                return true;
            }
        }
    }
    return matched;
}

static bool daemon_reload_config(openlogi_daemon_ctx_t *ctx)
{
    char err[256];
    openlogi_config_t *new_cfg = calloc(1, sizeof(openlogi_config_t));
    if (!new_cfg) {
        fprintf(stderr, "[openlogid] Out of memory reloading config\n");
        return false;
    }
    if (!config_load_file(g_config_file, new_cfg, err, sizeof(err))) {
        fprintf(stderr, "[openlogid] Failed to reload config: %s\n", err);
        free(new_cfg);
        return false;
    }
    button_runtime_cancel_all(&g_button_rt, BUTTON_END_CANCELED_INVALIDATED);
    config_free(&ctx->config);
    ctx->config = *new_cfg;
    free(new_cfg);

    if (ctx->runtime_state == RUNTIME_STATE_ACTIVE && hook_is_running()) {
        daemon_apply_diversion(ctx);
    }
    return true;
}

static void print_usage(const char *progname)
{
    printf("OpenLogi Pure C Daemon (openlogid) v%s\n", OPENLOGI_AGENT_VERSION);
    printf("Usage: %s [options]\n\n", progname);
    printf("Options:\n");
    printf("  -d, --daemon         Run daemon in background\n");
    printf("  -s, --socket <path>  Specify custom UNIX domain socket path\n");
    printf("  -c, --config <path>  Specify custom configuration TOML path\n");
    printf("  -l, --lock <path>    Specify custom lock file path\n");
    printf("  -v, --verbose        Enable verbose debug logging\n");
    printf("  -h, --help           Show this help message and exit\n");
}

int daemon_main(int argc, char **argv)
{
    bool run_as_daemon = false;
    optind = 1;

    static struct option long_options[] = {
        {"daemon",  no_argument,       0, 'd'},
        {"socket",  required_argument, 0, 's'},
        {"config",  required_argument, 0, 'c'},
        {"lock",    required_argument, 0, 'l'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "ds:c:l:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd': run_as_daemon = true; break;
            case 's': strncpy(g_socket_file, optarg, sizeof(g_socket_file) - 1); break;
            case 'c': strncpy(g_config_file, optarg, sizeof(g_config_file) - 1); break;
            case 'l': strncpy(g_lock_file, optarg, sizeof(g_lock_file) - 1); break;
            case 'v': g_verbose = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    /* Acquire single-instance process lock before doing anything else */
    pid_t existing_pid = 0;
    const char *lpath = (g_lock_file[0] != '\0') ? g_lock_file : NULL;
    int lock_fd = single_instance_acquire(lpath, &existing_pid);
    if (lock_fd < 0) {
        if (run_as_daemon) {
            /* Daemon already running in background: clean exit */
            return 0;
        }
        fprintf(stderr, "[openlogid] Error: Another instance of openlogid is already running (PID: %d). Exiting.\n", (int)existing_pid);
        return 1;
    }

    if (run_as_daemon) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            single_instance_release(lock_fd, lpath);
            return 1;
        }
        if (pid > 0) {
            /* Parent exits cleanly; child holds the inherited lock descriptor */
            return 0;
        }
        umask(027);
        setsid();
        /* Update the lock file with the child daemon's PID */
        single_instance_update_pid(lock_fd, getpid());
    }

#if defined(__APPLE__) || defined(__linux__)
    setlinebuf(stdout);
    setlinebuf(stderr);
#endif

    printf("[openlogid] Starting OpenLogi Daemon v%s (PID: %d)...\n", OPENLOGI_AGENT_VERSION, getpid());

    /* 1. Initialize Signal Handlers & Safety Registry */
    signals_init();
    inject_init();
    action_queue_init();
    swipe_accumulator_init(&g_gesture_acc);
    button_runtime_init(&g_button_rt, on_button_runtime_event, &g_daemon_ctx);

    /* 2. Determine Paths */
    if (g_socket_file[0] == '\0') {
        ipc_get_default_socket_path(g_socket_file, sizeof(g_socket_file));
    }
    if (g_config_file[0] == '\0') {
        config_get_default_path(g_config_file, sizeof(g_config_file));
    }

    char state_dir[512];
    strncpy(state_dir, g_socket_file, sizeof(state_dir) - 1);
    char *last_sl = strrchr(state_dir, '/');
    if (last_sl) *last_sl = '\0';
    snprintf(g_session_file, sizeof(g_session_file), "%s/session.state", state_dir);

    armed_session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.boot_uuid, "openlogi-session", sizeof(session.boot_uuid) - 1);
    session.audit_session_id = (uint32_t)getpid();
    armed_session_write(g_session_file, &session);

    /* 3. Setup Daemon Context */
    memset(&g_daemon_ctx, 0, sizeof(g_daemon_ctx));
    g_daemon_ctx.start_time = time(NULL);
    g_daemon_ctx.set_dpi = daemon_set_dpi;
    g_daemon_ctx.set_smartshift = daemon_set_smartshift;
    g_daemon_ctx.reload_config = daemon_reload_config;
    g_daemon_ctx.runtime_state = RUNTIME_STATE_STANDBY;

    /* 4. Load Configuration */
    char cfg_err[256];
    if (config_load_file(g_config_file, &g_daemon_ctx.config, cfg_err, sizeof(cfg_err))) {
        printf("[openlogid] Loaded configuration from %s\n", g_config_file);
    } else {
        printf("[openlogid] No existing configuration found or parse error (%s); using defaults.\n", cfg_err);
    }

    /* 5. Start IPC Server */
    if (!ipc_server_start(g_socket_file, &g_daemon_ctx)) {
        fprintf(stderr, "[openlogid] Failed to start IPC socket server at %s\n", g_socket_file);
        return 1;
    }
    printf("[openlogid] JSON-RPC 2.0 IPC server listening at %s\n", g_socket_file);

    /* 6. Start Config Watcher (shares IPC server event loop) */
    if (config_watch_start(g_config_file, on_config_reloaded, NULL)) {
        printf("[openlogid] Config file hot-reload watcher active.\n");
    }

    /* 7. Check system permissions (non-interactive in background) */
#ifdef __APPLE__
    bool has_ax = AXIsProcessTrusted();
    bool has_inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);
    if (!has_ax || !has_inp) {
        printf("[openlogid] Notice: Accessibility and/or Input Monitoring not yet granted. Operating in passive mode until authorized.\n");
    }
#endif

    /* 8. Start Background Hotplug Monitor & Probe Connected Hardware */
    hid_start_hotplug_monitor(on_hotplug_event, &g_daemon_ctx);
    daemon_probe_hardware(&g_daemon_ctx);
    daemon_reconcile_runtime_state(&g_daemon_ctx);

    printf("[openlogid] Daemon initialized successfully. Ready for clients (State: %s).\n",
           (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) ? "ACTIVE" : "STANDBY");

    /* Setup signal wakeup source on macOS CFRunLoop */
#ifdef __APPLE__
    CFFileDescriptorContext fd_ctx = { 0, NULL, NULL, NULL, NULL };
    int sig_fd = signals_get_wakeup_fd();
    CFFileDescriptorRef sig_cf_fd = NULL;
    CFRunLoopSourceRef sig_source = NULL;
    if (sig_fd >= 0) {
        sig_cf_fd = CFFileDescriptorCreate(kCFAllocatorDefault, sig_fd, false, NULL, &fd_ctx);
        if (sig_cf_fd) {
            sig_source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, sig_cf_fd, 0);
            if (sig_source) {
                CFRunLoopAddSource(CFRunLoopGetCurrent(), sig_source, kCFRunLoopDefaultMode);
            }
            CFFileDescriptorEnableCallBacks(sig_cf_fd, kCFFileDescriptorReadCallBack);
        }
    }
#endif

    uint64_t last_app_tick_ms = 0;
    uint64_t last_standby_scan_ms = 0;

    /* Main Daemon Loop */
    while (!signals_should_exit()) {
        CFTimeInterval timeout = (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) ? 0.05 : 2.0;
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, timeout, false);
#else
        if (timeout >= 1.0) {
            usleep(1000000);
        } else {
            usleep(50000);
        }
#endif
        if (signals_should_exit()) break;

        /* Process pending hotplug / state changes on main loop thread */
        if (g_pending_hotplug_probe) {
            g_pending_hotplug_probe = false;
            daemon_probe_hardware(&g_daemon_ctx);
            daemon_reconcile_runtime_state(&g_daemon_ctx);
            if (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) {
                daemon_apply_diversion(&g_daemon_ctx);
            }
        } else if (g_pending_reconcile) {
            g_pending_reconcile = false;
            daemon_reconcile_runtime_state(&g_daemon_ctx);
        }

        uint64_t now_ms = daemon_get_time_ms();

        if (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) {
            button_runtime_tick(&g_button_rt, now_ms);

            if (now_ms >= last_app_tick_ms + 100) {
                last_app_tick_ms = now_ms;
                app_watch_tick();
            }

            /* If the input hook could not be started at boot due to missing macOS permissions,
             * retry automatically once permissions are granted in System Settings. */
            if (!hook_is_running()) {
                static uint64_t last_hook_retry_ms = 0;
                if (now_ms >= last_hook_retry_ms + 1000) {
                    last_hook_retry_ms = now_ms;
#ifdef __APPLE__
                    if (AXIsProcessTrusted()) {
                        if (hook_init(on_hook_event, NULL) && hook_start()) {
                            printf("[openlogid] Accessibility permission granted! Input interception hook activated.\n");
                            daemon_apply_diversion(&g_daemon_ctx);
                            ipc_server_broadcast_event("hook_activated", "{\"active\":true}");
                        }
                    }
#else
                    if (hook_init(on_hook_event, NULL) && hook_start()) {
                        daemon_apply_diversion(&g_daemon_ctx);
                        ipc_server_broadcast_event("hook_activated", "{\"active\":true}");
                    }
#endif
                }
            }
        } else {
            /* In STANDBY: skip button ticks, app polling, and active scroll math completely.
             * If permissions were granted while in standby, initialize and activate the event tap hook. */
            if (!hook_is_running()) {
#ifdef __APPLE__
                if (AXIsProcessTrusted()) {
                    if (hook_init(on_hook_event, NULL) && hook_start()) {
                        printf("[openlogid] Accessibility permission granted! Input interception hook activated.\n");
                        ipc_server_broadcast_event("hook_activated", "{\"active\":true}");
                        daemon_reconcile_runtime_state(&g_daemon_ctx);
                    }
                }
#else
                if (hook_init(on_hook_event, NULL) && hook_start()) {
                    ipc_server_broadcast_event("hook_activated", "{\"active\":true}");
                    daemon_reconcile_runtime_state(&g_daemon_ctx);
                }
#endif
            }

            /* Perform bounded fallback hardware rescan every 5 seconds for sleeping paired devices. */
            if (now_ms >= last_standby_scan_ms + 5000) {
                last_standby_scan_ms = now_ms;
                daemon_probe_hardware(&g_daemon_ctx);
                daemon_reconcile_runtime_state(&g_daemon_ctx);
                if (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) {
                    daemon_apply_diversion(&g_daemon_ctx);
                }
            }
        }
    }

#ifdef __APPLE__
    if (sig_source) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), sig_source, kCFRunLoopDefaultMode);
        CFRelease(sig_source);
        sig_source = NULL;
    }
    if (sig_cf_fd) {
        CFFileDescriptorInvalidate(sig_cf_fd);
        CFRelease(sig_cf_fd);
        sig_cf_fd = NULL;
    }
#endif

    printf("\n[openlogid] Termination signal received. Performing graceful shutdown...\n");

    /* Clean Shutdown */
    config_watch_stop();
    ipc_server_stop();
    hook_stop();
    app_watch_stop();
    action_queue_shutdown();
    hidpp_diversion_teardown_all();
    hid_stop_hotplug_monitor();

    for (size_t i = 0; i < g_daemon_ctx.device_count; i++) {
        if (g_live_handles[i].dev_handle) {
            hid_close(g_live_handles[i].dev_handle);
            g_live_handles[i].dev_handle = NULL;
        }
        if (g_live_handles[i].secondary_handle) {
            hid_close(g_live_handles[i].secondary_handle);
            g_live_handles[i].secondary_handle = NULL;
        }
    }
    hid_transport_shutdown();
    config_free(&g_daemon_ctx.config);
    armed_session_clear(g_session_file);

    single_instance_release(lock_fd, lpath);

    printf("[openlogid] Shutdown complete. Goodbye.\n");
    return 0;
}
