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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

static openlogi_daemon_ctx_t g_daemon_ctx;
static char g_config_file[512] = { 0 };
static char g_socket_file[512] = { 0 };
static char g_lock_file[512] = { 0 };
static bool g_verbose = false;

static void on_config_reloaded(const char *filepath, void *user_data)
{
    (void)user_data;
    printf("[openlogid] Configuration file modified (%s). Hot-reloading...\n", filepath);
    char err[256];
    if (config_load_file(filepath, &g_daemon_ctx.config, err, sizeof(err))) {
        printf("[openlogid] Configuration reloaded successfully (schema version %u).\n",
               g_daemon_ctx.config.schema_version);
        ipc_server_broadcast_event("config_reloaded", "{\"success\":true}");
    } else {
        fprintf(stderr, "[openlogid] Failed to reload config: %s\n", err);
    }
}

static void on_app_changed(const char *app_id, void *user_data)
{
    (void)user_data;
    if (g_verbose) {
        printf("[openlogid] Active foreground app: %s\n", app_id);
    }
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

    inject_action(action);
}

static void daemon_dispatch_gesture(openlogi_daemon_ctx_t *ctx, config_button_id_t btn_id, gesture_direction_t dir)
{
    char front_app[256] = { 0 };
    app_watch_get_frontmost(front_app, sizeof(front_app));

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

static hook_action_t on_hook_event(const hook_event_t *event, void *user_data)
{
    (void)user_data;
    if (!event) return HOOK_ACTION_PASS;

    /* Map intercepted OS mouse event to button identifier */
    config_button_id_t btn_id = CONFIG_BUTTON_UNKNOWN;
    if (event->type == HOOK_EVENT_BUTTON_DOWN) {
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
        app_watch_get_frontmost(front_app, sizeof(front_app));

        const config_button_binding_t *binding = config_resolve_binding(
            &g_daemon_ctx.config, NULL, front_app, btn_id
        );

        if (binding && binding->action.type != ACTION_NONE) {
            char desc[128];
            snprintf(desc, sizeof(desc), "OS Mouse Button %u (app: %s)", event->button_number, front_app);
            daemon_dispatch_action(&g_daemon_ctx, &binding->action, desc);
            return HOOK_ACTION_SUPPRESS;
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
            if (cid == HIDPP_CID_GESTURE_BUTTON) {
                thumb_down = true;
            } else if (cid == HIDPP_CID_MODE_SHIFT_SMARTSHIFT ||
                       cid == HIDPP_CID_DPI_SWITCH_ED ||
                       cid == HIDPP_CID_DPI_SWITCH_FD) {
                dpi_toggle_down = true;
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
                            if (g_live_handles[d].feat_reprog != 0) {
                                hidpp_reprog_set_reporting(
                                    sec_dev,
                                    g_live_handles[d].device_index,
                                    g_live_handles[d].feat_reprog,
                                    g_live_handles[d].sw_id,
                                    HIDPP_CID_GESTURE_BUTTON,
                                    true,
                                    true
                                );
                                hidpp_reprog_set_reporting(
                                    sec_dev,
                                    g_live_handles[d].device_index,
                                    g_live_handles[d].feat_reprog,
                                    g_live_handles[d].sw_id,
                                    HIDPP_CID_MODE_SHIFT_SMARTSHIFT,
                                    true,
                                    true
                                );
                            }
                            hid_register_report_callback(sec_dev, on_hid_report_event, ctx);
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

            /* Divert gesture controls if feature 0x1B04 is supported */
            if (probed.feat_reprog_controls != 0) {
                bool g_ok = hidpp_reprog_set_reporting(
                    dev,
                    HIDPP_DEVICE_RECEIVER,
                    probed.feat_reprog_controls,
                    HIDPP_DEFAULT_SW_ID,
                    HIDPP_CID_GESTURE_BUTTON,
                    true,  /* divert to software */
                    true   /* raw_xy streaming */
                );
                bool m_ok = hidpp_reprog_set_reporting(
                    dev,
                    HIDPP_DEVICE_RECEIVER,
                    probed.feat_reprog_controls,
                    HIDPP_DEFAULT_SW_ID,
                    HIDPP_CID_MODE_SHIFT_SMARTSHIFT,
                    true,  /* divert to software */
                    true   /* raw_xy streaming */
                );

                hid_register_report_callback(dev, on_hid_report_event, ctx);

                printf("[openlogid] ReprogControls (0x1B04) diverted for %s: GestureButton=%s, ModeShift=%s\n",
                       st->product_name, g_ok ? "diverted" : "err", m_ok ? "diverted" : "err");
            }

            ctx->device_count++;

            printf("[openlogid] Discovered hardware: %s (DPI: %u, Battery: %u%%, SmartShift: %u)\n",
                   st->product_name, st->dpi, st->battery_percent, st->smartshift_mode);
        } else {
            hid_close(dev);
        }
    }
}

static bool daemon_set_dpi(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint16_t dpi)
{
    if (g_verbose) {
        printf("[openlogid] Setting DPI to %u for device %s\n", dpi, dev_key ? dev_key : "default");
    }

    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            ctx->devices[i].dpi = dpi;
            if (g_live_handles[i].dev_handle && g_live_handles[i].feat_dpi != 0) {
                hidpp_write_dpi(g_live_handles[i].dev_handle,
                                g_live_handles[i].device_index,
                                g_live_handles[i].feat_dpi,
                                g_live_handles[i].sw_id,
                                dpi);
            }
            return true;
        }
    }
    return true;
}

static bool daemon_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t threshold)
{
    if (g_verbose) {
        printf("[openlogid] Setting SmartShift mode=%u thresh=%u for device %s\n",
               mode, threshold, dev_key ? dev_key : "default");
    }

    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            ctx->devices[i].smartshift_mode = mode;
            ctx->devices[i].smartshift_threshold = threshold;
            if (g_live_handles[i].dev_handle && g_live_handles[i].feat_smartshift != 0) {
                hidpp_write_smartshift(g_live_handles[i].dev_handle,
                                       g_live_handles[i].device_index,
                                       g_live_handles[i].feat_smartshift,
                                       g_live_handles[i].sw_id,
                                       (smartshift_mode_t)mode,
                                       threshold);
            }
            return true;
        }
    }
    return true;
}

static bool daemon_reload_config(openlogi_daemon_ctx_t *ctx)
{
    char err[256];
    return config_load_file(g_config_file, &ctx->config, err, sizeof(err));
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

    printf("[openlogid] Starting OpenLogi Daemon v%s (PID: %d)...\n", OPENLOGI_AGENT_VERSION, getpid());

    /* 1. Initialize Signal Handlers & Safety Registry */
    signals_init();
    inject_init();
    swipe_accumulator_init(&g_gesture_acc);

    /* 2. Determine Paths */
    if (g_socket_file[0] == '\0') {
        ipc_get_default_socket_path(g_socket_file, sizeof(g_socket_file));
    }
    if (g_config_file[0] == '\0') {
        config_get_default_path(g_config_file, sizeof(g_config_file));
    }

    /* 3. Setup Daemon Context */
    memset(&g_daemon_ctx, 0, sizeof(g_daemon_ctx));
    g_daemon_ctx.start_time = time(NULL);
    g_daemon_ctx.set_dpi = daemon_set_dpi;
    g_daemon_ctx.set_smartshift = daemon_set_smartshift;
    g_daemon_ctx.reload_config = daemon_reload_config;

    /* 4. Load Configuration */
    char cfg_err[256];
    if (config_load_file(g_config_file, &g_daemon_ctx.config, cfg_err, sizeof(cfg_err))) {
        printf("[openlogid] Loaded configuration from %s\n", g_config_file);
    } else {
        printf("[openlogid] No existing configuration found or parse error (%s); using defaults.\n", cfg_err);
    }

    /* 5. Start Config Watcher */
    if (config_watch_start(g_config_file, on_config_reloaded, NULL)) {
        printf("[openlogid] Config file hot-reload watcher active.\n");
    }

    /* 6. Start Foreground App Watcher */
    if (app_watch_start(on_app_changed, NULL)) {
        printf("[openlogid] Foreground application profile watcher active.\n");
    }

    /* 7. Start OS Event Tap Hook */
    if (hook_init(on_hook_event, NULL) && hook_start()) {
        printf("[openlogid] OS input interception hook installed.\n");
    } else {
        printf("[openlogid] Notice: Accessibility permission not yet granted; hook in passive mode.\n");
    }

    /* 8. Probe Connected Logitech Hardware */
    daemon_probe_hardware(&g_daemon_ctx);

    /* 9. Start IPC Server */
    if (!ipc_server_start(g_socket_file, &g_daemon_ctx)) {
        fprintf(stderr, "[openlogid] Failed to start IPC socket server at %s\n", g_socket_file);
        return 1;
    }
    printf("[openlogid] JSON-RPC 2.0 IPC server listening at %s\n", g_socket_file);
    printf("[openlogid] Daemon initialized successfully. Ready for clients.\n");

    /* Main Daemon Loop */
    int loop_tick = 0;
    while (!signals_should_exit()) {
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        usleep(200000); /* 200ms tick */
#endif
        loop_tick++;
        /* Periodically re-scan hardware if no devices were online at launch (every ~2 seconds) */
        if (g_daemon_ctx.device_count == 0 && (loop_tick % 10 == 0)) {
            daemon_probe_hardware(&g_daemon_ctx);
        }
    }

    printf("\n[openlogid] Termination signal received. Performing graceful shutdown...\n");

    /* Clean Shutdown */
    ipc_server_stop();
    hook_stop();
    app_watch_stop();
    config_watch_stop();
    hidpp_diversion_teardown_all();

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

    single_instance_release(lock_fd, lpath);

    printf("[openlogid] Shutdown complete. Goodbye.\n");
    return 0;
}
