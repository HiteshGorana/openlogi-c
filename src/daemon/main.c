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
            if (g_verbose) {
                printf("[openlogid] Button %u triggered action %d (active app: %s)\n",
                       event->button_number, (int)binding->action.type, front_app);
            }
            inject_action(&binding->action);
            return HOOK_ACTION_SUPPRESS;
        }
    }

    return HOOK_ACTION_PASS;
}

typedef struct {
    hid_device_t *dev_handle;
    uint8_t device_index;
    uint8_t sw_id;
    uint8_t feat_dpi;
    uint8_t feat_smartshift;
    uint8_t feat_reprog;
} live_device_handle_t;

static live_device_handle_t g_live_handles[OPENLOGI_MAX_DEVICES];

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

        /* Deduplicate if serial already probed */
        if (info->serial_number[0] != '\0') {
            bool already = false;
            for (size_t d = 0; d < ctx->device_count; d++) {
                if (strcmp(ctx->devices[d].serial_number, info->serial_number) == 0) {
                    already = true;
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
            g_live_handles[idx].device_index = HIDPP_DEVICE_RECEIVER;
            g_live_handles[idx].sw_id = HIDPP_DEFAULT_SW_ID;
            g_live_handles[idx].feat_dpi = probed.feat_dpi;
            g_live_handles[idx].feat_smartshift = probed.feat_smartshift;
            g_live_handles[idx].feat_reprog = probed.feat_reprog_controls;

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

int main(int argc, char **argv)
{
    bool run_as_daemon = false;

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
    }
    hid_transport_shutdown();

    single_instance_release(lock_fd, lpath);

    printf("[openlogid] Shutdown complete. Goodbye.\n");
    return 0;
}
