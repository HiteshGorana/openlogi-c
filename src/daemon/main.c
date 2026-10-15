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
#include "input_runtime.h"
#include "daemon_safety.h"
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
#include <pthread.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <objc/runtime.h>
#include <objc/message.h>
extern bool CGPreflightListenEventAccess(void);
extern bool CGRequestListenEventAccess(void);
#endif

static openlogi_daemon_ctx_t g_daemon_ctx;
static char g_config_file[512] = { 0 };
static char g_socket_file[512] = { 0 };
static char g_lock_file[512] = { 0 };
static char g_session_file[512] = { 0 };
static bool g_verbose = false;
static input_runtime_t g_hook_input;
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
static bool daemon_apply_diversion(openlogi_daemon_ctx_t *ctx);
static void daemon_cancel_input(void);
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
    /* Existing presses retain their selected action; capture follows the new app. */
    g_pending_reconcile = true;
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"app_id\":\"%s\"}", app_id);
    ipc_server_broadcast_event("app_activated", payload);
}


static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;

/* Forward declarations */
static bool daemon_set_dpi(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint16_t dpi);
static bool daemon_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t threshold);

typedef struct {
    openlogi_daemon_ctx_t *ctx;
    char device_key[128];
    uint64_t generation;
} device_dispatch_ctx_t;

typedef struct {
    hid_device_t *dev_handle;
    hid_device_t *secondary_handle;
    char primary_transport[256];
    char secondary_transport[256];
    uint8_t device_index;
    uint8_t sw_id;
    uint8_t feat_dpi;
    uint8_t feat_smartshift;
    uint8_t feat_reprog;
    uint8_t feat_hires_wheel;
    input_runtime_t input;
    capture_spec_t captured;
    hidpp_cid_info_t controls[32];
    uint8_t control_count;
    bool controls_loaded;
    uint64_t generation;
    device_dispatch_ctx_t dispatch_ctx;
} live_device_handle_t;

static live_device_handle_t g_live_handles[OPENLOGI_MAX_DEVICES];

static ssize_t find_device_index(const openlogi_daemon_ctx_t *ctx, const char *dev_key)
{
    if (!ctx || ctx->device_count == 0) return -1;
    if (dev_key && dev_key[0] != '\0' && strcmp(dev_key, "default") != 0) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            if (strcmp(ctx->devices[i].device_key, dev_key) == 0) return (ssize_t)i;
        }
        return -1;
    }
    if (ctx->config.selected_device[0] != '\0') {
        for (size_t i = 0; i < ctx->device_count; i++) {
            if (strcmp(ctx->devices[i].device_key, ctx->config.selected_device) == 0)
                return (ssize_t)i;
        }
    }
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (ctx->devices[i].online) return (ssize_t)i;
    }
    return 0;
}

static const config_device_t *daemon_get_active_device_config(const openlogi_daemon_ctx_t *ctx)
{
    if (!ctx || ctx->config.device_count == 0) return NULL;
    const char *sel = ctx->config.selected_device;
    if (sel[0] != '\0') {
        const config_device_t *cdev = config_resolve_device(&ctx->config, sel);
        if (cdev) return cdev;
    }
    return config_resolve_device(&ctx->config, NULL);
}

static void daemon_dispatch_action(openlogi_daemon_ctx_t *ctx, const openlogi_action_t *action,
                                   const char *source_desc, const char *target_device_key)
{
    if (!action || action->type == ACTION_NONE) return;

    if (g_verbose) {
        printf("[openlogid] Action %d triggered via %s (device: %s)\n",
               (int)action->type, source_desc ? source_desc : "unknown",
               target_device_key ? target_device_key : "default");
    }

    if (action->type == ACTION_TOGGLE_SMARTSHIFT) {
        const char *dev_key = target_device_key;
        if (!dev_key && ctx->device_count > 0) {
            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = daemon_get_active_device_config(ctx);
            dev_key = (cdev && cdev->device_key[0]) ? cdev->device_key : ctx->devices[0].device_key;
            pthread_mutex_unlock(&g_config_lock);
        }
        if (dev_key) {
            ssize_t idx = find_device_index(ctx, dev_key);
            if (idx >= 0 && (size_t)idx < ctx->device_count) {
                uint8_t cur = ctx->devices[idx].smartshift_mode;
                uint8_t next = (cur == 1) ? 2 : 1;
                daemon_set_smartshift(ctx, dev_key, next, ctx->devices[idx].smartshift_threshold);
                char payload[256];
                snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"toggled\":true,\"mode\":%u}", dev_key, next);
                ipc_server_broadcast_event("smartshift_changed", payload);
            }
        }
        return;
    }

    if (action->type == ACTION_CYCLE_DPI) {
        const char *dev_key = target_device_key;
        if (!dev_key && ctx->device_count > 0) {
            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = daemon_get_active_device_config(ctx);
            dev_key = (cdev && cdev->device_key[0]) ? cdev->device_key : ctx->devices[0].device_key;
            pthread_mutex_unlock(&g_config_lock);
        }
        if (dev_key) {
            ssize_t idx = find_device_index(ctx, dev_key);
            if (idx >= 0 && (size_t)idx < ctx->device_count) {
                uint16_t cur_dpi = ctx->devices[idx].dpi;
                uint16_t next_dpi = (cur_dpi < 1600) ? 1600 : (cur_dpi < 2400 ? 2400 : 1000);
                pthread_mutex_lock(&g_config_lock);
                const config_device_t *cdev = config_resolve_device(&ctx->config, dev_key);
                if (cdev && cdev->dpi_preset_count > 0) {
                    for (size_t p = 0; p < cdev->dpi_preset_count; p++) {
                        if (cdev->dpi_presets[p] == cur_dpi) {
                            next_dpi = cdev->dpi_presets[(p + 1) % cdev->dpi_preset_count];
                            break;
                        }
                    }
                }
                pthread_mutex_unlock(&g_config_lock);
                daemon_set_dpi(ctx, dev_key, next_dpi);
                char payload[256];
                snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"cycled\":true,\"dpi\":%u}", dev_key, next_dpi);
                ipc_server_broadcast_event("dpi_changed", payload);
            }
        }
        return;
    }

    if (action->type == ACTION_SET_DPI_PRESET) {
        const char *dev_key = target_device_key;
        if (!dev_key && ctx->device_count > 0) {
            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = daemon_get_active_device_config(ctx);
            dev_key = (cdev && cdev->device_key[0]) ? cdev->device_key : ctx->devices[0].device_key;
            pthread_mutex_unlock(&g_config_lock);
        }
        if (dev_key) {
            uint8_t p_idx = action->u.dpi_preset_index;
            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = config_resolve_device(&ctx->config, dev_key);
            if (cdev && p_idx < cdev->dpi_preset_count) {
                uint16_t target = cdev->dpi_presets[p_idx];
                pthread_mutex_unlock(&g_config_lock);
                daemon_set_dpi(ctx, dev_key, target);
                char payload[256];
                snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"dpi\":%u}", dev_key, target);
                ipc_server_broadcast_event("dpi_changed", payload);
            } else {
                pthread_mutex_unlock(&g_config_lock);
            }
        }
        return;
    }

    if (action_queue_is_blocking(action)) {
        if (g_verbose) {
            printf("[openlogid] Offloading blocking action to async worker\n");
        }
        if (!action_queue_enqueue(action))
            ipc_server_broadcast_event("action_rejected", "{\"reason\":\"queue_full_or_stopping\"}");
    } else {
        inject_action(action);
    }
}

static void daemon_device_input_dispatch(void *context, config_button_id_t button,
                                         gesture_direction_t direction, const openlogi_action_t *action)
{
    device_dispatch_ctx_t *dctx = (device_dispatch_ctx_t *)context;
    if (!dctx || !dctx->ctx) return;
    openlogi_daemon_ctx_t *ctx = dctx->ctx;
    const char *dev_key = dctx->device_key[0] ? dctx->device_key : NULL;

    daemon_dispatch_action(ctx, action, button_id_to_string(button), dev_key);
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"button\":\"%s\",\"direction\":\"%s\"}",
             dev_key ? dev_key : "default",
             button_id_to_string(button), gesture_direction_name(direction));
    ipc_server_broadcast_event("button_triggered", payload);
}

static void daemon_hook_input_dispatch(void *context, config_button_id_t button,
                                       gesture_direction_t direction, const openlogi_action_t *action)
{
    openlogi_daemon_ctx_t *ctx = (openlogi_daemon_ctx_t *)context;
    if (!ctx) return;

    pthread_mutex_lock(&g_config_lock);
    const config_device_t *cdev = daemon_get_active_device_config(ctx);
    char dev_key[128] = { 0 };
    if (cdev && cdev->device_key[0]) {
        strncpy(dev_key, cdev->device_key, sizeof(dev_key) - 1);
    }
    pthread_mutex_unlock(&g_config_lock);

    daemon_dispatch_action(ctx, action, button_id_to_string(button), dev_key[0] ? dev_key : NULL);
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"button\":\"%s\",\"direction\":\"%s\"}",
             dev_key[0] ? dev_key : "default",
             button_id_to_string(button), gesture_direction_name(direction));
    ipc_server_broadcast_event("button_triggered", payload);
}

static hook_action_t on_hook_event(const hook_event_t *event, void *user_data)
{
    (void)user_data;
    if (!event || g_daemon_ctx.runtime_state != RUNTIME_STATE_ACTIVE) return HOOK_ACTION_PASS;
    if (event->type == HOOK_EVENT_SCROLL) {
        pthread_mutex_lock(&g_config_lock);
        const config_device_t *device = daemon_get_active_device_config(&g_daemon_ctx);
        bool invert = device && device->enabled && device->invert_scroll;
        pthread_mutex_unlock(&g_config_lock);
        return invert ? HOOK_ACTION_INVERT : HOOK_ACTION_PASS;
    }
    config_button_id_t button = CONFIG_BUTTON_UNKNOWN;
    if (event->button_number == 3) button = CONFIG_BUTTON_MIDDLE;
    else if (event->button_number == 4) button = CONFIG_BUTTON_BACK;
    else if (event->button_number == 5) button = CONFIG_BUTTON_FORWARD;
    else if (event->button_number == 6) button = CONFIG_BUTTON_GESTURE;
    else if (event->button_number == 7) button = CONFIG_BUTTON_DPI_TOGGLE;
    if (event->type == HOOK_EVENT_MOUSE_DRAG)
        return input_runtime_captured(&g_hook_input, button) ? HOOK_ACTION_SUPPRESS : HOOK_ACTION_PASS;
    if (event->type != HOOK_EVENT_BUTTON_DOWN && event->type != HOOK_EVENT_BUTTON_UP) return HOOK_ACTION_PASS;
    char app[256] = { 0 };
    app_watch_get_cached(app, sizeof(app));

    pthread_mutex_lock(&g_config_lock);
    const config_button_binding_t *binding = config_resolve_binding(&g_daemon_ctx.config, NULL, app, button);
    /* Raw gesture movement requires HID delivery, not the global OS fallback. */
    if (binding && binding->is_gesture) binding = NULL;
    bool suppress = input_runtime_button(&g_hook_input, button, event->type == HOOK_EVENT_BUTTON_DOWN, binding);
    pthread_mutex_unlock(&g_config_lock);

    return suppress ? HOOK_ACTION_SUPPRESS : HOOK_ACTION_PASS;
}

static void daemon_cancel_input(void)
{
    input_runtime_cancel(&g_hook_input);
    for (size_t i = 0; i < OPENLOGI_MAX_DEVICES; i++) input_runtime_cancel(&g_live_handles[i].input);
    action_queue_clear_pending();
}

static void on_hid_report_event(hid_device_t *dev, const hidpp_raw_report_t *raw, void *user_data)
{
    openlogi_daemon_ctx_t *ctx = user_data;
    if (!dev || !raw || !ctx || ctx->runtime_state != RUNTIME_STATE_ACTIVE) return;
    for (size_t i = 0; i < ctx->device_count; i++) {
        live_device_handle_t *handle = &g_live_handles[i];
        if (handle->dev_handle != dev && handle->secondary_handle != dev) continue;
        char app[256] = { 0 };
        app_watch_get_cached(app, sizeof(app));
        pthread_mutex_lock(&g_config_lock);
        input_runtime_report(&handle->input, raw, handle->feat_reprog,
                             &ctx->config, ctx->devices[i].device_key, app);
        pthread_mutex_unlock(&g_config_lock);
        break;
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

static bool write_capture(void *context, uint16_t cid, bool divert, bool raw_xy)
{
    live_device_handle_t *handle = context;
    return hidpp_reprog_set_reporting(handle->dev_handle, handle->device_index,
        handle->feat_reprog, handle->sw_id, cid, divert, raw_xy);
}

static bool daemon_apply_diversion(openlogi_daemon_ctx_t *ctx)
{
    if (!ctx || !hook_is_running()) return false;
    char app[256] = { 0 };
    app_watch_get_cached(app, sizeof(app));
    bool ok = true;
    for (size_t i = 0; i < ctx->device_count; i++) {
        live_device_handle_t *handle = &g_live_handles[i];
        if (!ctx->devices[i].online || !handle->dev_handle || !handle->feat_reprog) continue;
        const config_device_t *device = config_resolve_device(&ctx->config, ctx->devices[i].device_key);
        capture_spec_t logical, desired;
        capture_plan_resolve_for_app(device, app, &logical);
        capture_spec_init(&desired);
        if (logical.divert_count && !handle->controls_loaded) {
            uint8_t count = 0;
            if (!hidpp_reprog_get_count(handle->dev_handle, handle->device_index,
                                       handle->feat_reprog, handle->sw_id, &count) || count > 32) return false;
            for (uint8_t c = 0; c < count; c++) {
                if (!hidpp_reprog_get_cid_info(handle->dev_handle, handle->device_index,
                    handle->feat_reprog, handle->sw_id, c, &handle->controls[c])) return false;
            }
            handle->control_count = count;
            handle->controls_loaded = true;
        }
        for (size_t c = 0; c < handle->control_count; c++) {
            uint16_t cid = handle->controls[c].cid;
            config_button_id_t button = input_button_for_cid(cid);
            for (size_t n = 0; n < logical.divert_count; n++) {
                if (button != CONFIG_BUTTON_UNKNOWN && button == input_button_for_cid(logical.divert_cids[n])) {
                    capture_spec_add_cid(&desired, cid);
                    desired.wants_raw_xy = logical.wants_raw_xy;
                }
            }
        }
        /* Register delivery before taking any native control away. Secondary
         * collections deliver the same device events; never capture them blindly. */
        hid_register_report_callback(handle->dev_handle, on_hid_report_event, ctx);
        if (handle->secondary_handle) hid_register_report_callback(handle->secondary_handle, on_hid_report_event, ctx);
        if (!capture_plan_apply(&handle->captured, &desired, write_capture, handle)) ok = false;
    }
    return ok;
}

static void daemon_undivert_all(void)
{
    hidpp_diversion_teardown_all();
    for (size_t i = 0; i < OPENLOGI_MAX_DEVICES; i++) capture_spec_init(&g_live_handles[i].captured);
}

static bool safety_start_hook(void *ctx) { (void)ctx; return hook_init(on_hook_event, NULL) && hook_start(); }
static void safety_stop_hook(void *ctx) { (void)ctx; hook_stop(); }
static void safety_start_watch(void *ctx) { (void)ctx; if (!app_watch_is_running()) app_watch_start(on_app_changed, NULL); }
static void safety_stop_watch(void *ctx) { (void)ctx; app_watch_stop(); }
static void safety_cancel(void *ctx) { (void)ctx; daemon_cancel_input(); }
static bool safety_capture(void *ctx) { return daemon_apply_diversion(ctx); }
static void safety_restore(void *ctx) { (void)ctx; daemon_undivert_all(); }
static void safety_stop_jobs(void *ctx) { (void)ctx; action_queue_shutdown(); }
static void safety_cancel_jobs(void *ctx) { (void)ctx; action_queue_cancel(); }
static const daemon_safety_ops_t safety_ops = { safety_start_hook, safety_stop_hook, safety_start_watch,
    safety_stop_watch, safety_cancel, safety_capture, safety_restore, safety_stop_jobs, safety_cancel_jobs };

static void daemon_reconcile_runtime_state(openlogi_daemon_ctx_t *ctx)
{
    if (!ctx) return;
    bool permissions = hook_check_permission();
#ifdef __APPLE__
    permissions = permissions && IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted;
#endif
    openlogi_runtime_state_t previous = ctx->runtime_state;
    ctx->runtime_state = RUNTIME_STATE_STANDBY; /* Gate input during arming/restoration. */
    bool active = daemon_safety_reconcile(count_online_usable_devices(ctx), permissions,
                                          hook_is_running(), &safety_ops, ctx);
    ctx->runtime_state = active ? RUNTIME_STATE_ACTIVE : RUNTIME_STATE_STANDBY;
    if (previous != ctx->runtime_state)
        ipc_server_broadcast_event("runtime_state_changed", active ? "{\"state\":\"active\"}" : "{\"state\":\"standby\"}");
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

    daemon_cancel_input();
    daemon_undivert_all();
    memset(g_live_handles, 0, sizeof(g_live_handles));
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
                            strncpy(g_live_handles[d].secondary_transport, info->transport_path, sizeof(g_live_handles[d].secondary_transport) - 1);
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
                char base_key[64];
                snprintf(base_key, sizeof(base_key), "dev:0x%04x:0x%04x", info->vendor_id, info->product_id);
                int count_same = 0;
                for (size_t d = 0; d < ctx->device_count; d++) {
                    if (strncmp(ctx->devices[d].device_key, base_key, strlen(base_key)) == 0) {
                        count_same++;
                    }
                }
                if (count_same == 0) {
                    snprintf(st->device_key, sizeof(st->device_key), "%s", base_key);
                } else {
                    snprintf(st->device_key, sizeof(st->device_key), "%s:%d", base_key, count_same + 1);
                }
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

            g_live_handles[idx].generation++;
            g_live_handles[idx].dispatch_ctx.ctx = ctx;
            strncpy(g_live_handles[idx].dispatch_ctx.device_key, st->device_key, sizeof(g_live_handles[idx].dispatch_ctx.device_key) - 1);
            g_live_handles[idx].dispatch_ctx.generation = g_live_handles[idx].generation;

            input_runtime_init(&g_live_handles[idx].input, daemon_device_input_dispatch, &g_live_handles[idx].dispatch_ctx);
            g_live_handles[idx].dev_handle = dev;
            g_live_handles[idx].secondary_handle = NULL;
            strncpy(g_live_handles[idx].primary_transport, info->transport_path, sizeof(g_live_handles[idx].primary_transport) - 1);
            g_live_handles[idx].secondary_transport[0] = '\0';
            g_live_handles[idx].device_index = HIDPP_DEVICE_RECEIVER;
            g_live_handles[idx].sw_id = HIDPP_DEFAULT_SW_ID;
            g_live_handles[idx].feat_dpi = probed.feat_dpi;
            g_live_handles[idx].feat_smartshift = probed.feat_smartshift;
            g_live_handles[idx].feat_reprog = probed.feat_reprog_controls;
            g_live_handles[idx].feat_hires_wheel = probed.feat_hires_wheel;

            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = config_resolve_device(&ctx->config, st->device_key);
            bool inv_scroll = cdev ? cdev->invert_scroll : false;
            pthread_mutex_unlock(&g_config_lock);

            /* Configure hardware hi-res wheel inversion if Feature 0x2121 is supported */
            if (probed.feat_hires_wheel != 0) {
                hidpp_v20_msg_t hw_req;
                hires_wheel_build_mode_request(
                    probed.feat_hires_wheel,
                    HIDPP_DEFAULT_SW_ID,
                    inv_scroll,
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
            pthread_mutex_lock(&g_config_lock);
            const config_device_t *cdev = config_resolve_device(&ctx->config, ctx->devices[idx].device_key);
            uint16_t target_dpi = cdev ? cdev->dpi : 0;
            uint8_t ss_mode = cdev ? cdev->smartshift_mode : 0;
            uint8_t ss_thresh = cdev ? cdev->smartshift_threshold : 0;
            pthread_mutex_unlock(&g_config_lock);

            if (target_dpi > 0 && ctx->devices[idx].dpi != target_dpi) {
                daemon_set_dpi(ctx, ctx->devices[idx].device_key, target_dpi);
            }
            if (ss_thresh > 0) {
                daemon_set_smartshift(ctx, ctx->devices[idx].device_key, ss_mode, ss_thresh);
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
            if (g_live_handles[i].primary_transport[0] != '\0' &&
                strcmp(g_live_handles[i].primary_transport, info->transport_path) == 0) {
                match = true;
            } else if (g_live_handles[i].secondary_transport[0] != '\0' &&
                       strcmp(g_live_handles[i].secondary_transport, info->transport_path) == 0) {
                if (g_live_handles[i].secondary_handle) {
                    hidpp_diversion_untrack_device(g_live_handles[i].secondary_handle);
                    g_live_handles[i].secondary_handle = NULL;
                }
                g_live_handles[i].secondary_transport[0] = '\0';
                continue;
            } else if (info->serial_number[0] != '\0' && ctx->devices[i].serial_number[0] != '\0') {
                match = (strcmp(ctx->devices[i].serial_number, info->serial_number) == 0);
            } else if (ctx->devices[i].vendor_id == info->vendor_id &&
                       ctx->devices[i].product_id == info->product_id &&
                       ctx->devices[i].online) {
                match = true;
            }

            if (match) {
                ctx->devices[i].online = false;
                g_live_handles[i].generation++;
                g_live_handles[i].dispatch_ctx.generation = g_live_handles[i].generation;
                if (g_live_handles[i].dev_handle) {
                    hidpp_diversion_untrack_device(g_live_handles[i].dev_handle);
                    g_live_handles[i].dev_handle = NULL;
                }
                if (g_live_handles[i].secondary_handle) {
                    hidpp_diversion_untrack_device(g_live_handles[i].secondary_handle);
                    g_live_handles[i].secondary_handle = NULL;
                }
                g_live_handles[i].primary_transport[0] = '\0';
                g_live_handles[i].secondary_transport[0] = '\0';

                /* Per-device disconnect cleanup: cancels only this device's input runtime */
                input_runtime_cancel(&g_live_handles[i].input);
                capture_spec_init(&g_live_handles[i].captured);

                char payload[128];
                snprintf(payload, sizeof(payload), "{\"device_key\":\"%s\",\"online\":false}", ctx->devices[i].device_key);
                ipc_server_broadcast_event("device_disconnected", payload);
                break;
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

    ssize_t idx = find_device_index(ctx, dev_key);
    if (idx < 0 || (size_t)idx >= ctx->device_count) {
        return false;
    }
    size_t i = (size_t)idx;
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

static bool daemon_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t threshold)
{
    if (g_verbose) {
        printf("[openlogid] Setting SmartShift mode=%u thresh=%u for device %s\n",
               mode, threshold, dev_key ? dev_key : "default");
    }

    ssize_t idx = find_device_index(ctx, dev_key);
    if (idx < 0 || (size_t)idx >= ctx->device_count) {
        return false;
    }
    size_t i = (size_t)idx;
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

    pthread_mutex_lock(&g_config_lock);
    openlogi_config_t old_cfg = ctx->config;
    ctx->config = *new_cfg;
    free(new_cfg);
    pthread_mutex_unlock(&g_config_lock);

    config_free(&old_cfg);

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

#ifdef __APPLE__
    /* Check in with LaunchServices/WindowServer as a prohibited background process.
     * This registers the process immediately so LaunchServices knows it is running,
     * while Prohibited policy ensures Finder never routes GUI AppleEvents here or flags
     * the app as "Not Responding" when OpenLogi.app is opened. */
    id pool = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSAutoreleasePool"), sel_registerName("new"));
    id app = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (app) {
        ((void (*)(id, SEL, long))objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 2); /* NSApplicationActivationPolicyProhibited */
        ((void (*)(id, SEL))objc_msgSend)(app, sel_registerName("finishLaunching"));
    }
    if (pool) {
        ((void (*)(id, SEL))objc_msgSend)(pool, sel_registerName("drain"));
    }
#endif

    printf("[openlogid] Starting OpenLogi Daemon v%s (PID: %d)...\n", OPENLOGI_AGENT_VERSION, getpid());

    /* 1. Initialize Signal Handlers & Safety Registry */
    signals_init();
    inject_init();
    action_queue_init();
    input_runtime_init(&g_hook_input, daemon_hook_input_dispatch, &g_daemon_ctx);

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
    uint64_t last_safety_check_ms = 0;

    /* Main Daemon Loop */
    while (!signals_should_exit()) {
        double timeout = (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) ? 0.05 : 2.0;
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
        if (now_ms >= last_safety_check_ms + 1000) {
            last_safety_check_ms = now_ms;
            daemon_reconcile_runtime_state(&g_daemon_ctx);
        }

        if (g_daemon_ctx.runtime_state == RUNTIME_STATE_ACTIVE) {

            if (now_ms >= last_app_tick_ms + 100) {
                last_app_tick_ms = now_ms;
                app_watch_tick();
            }

        } else {
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
    g_daemon_ctx.runtime_state = RUNTIME_STATE_STANDBY;
    daemon_safety_stop(&safety_ops, &g_daemon_ctx);
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
