/**
 * @file openlogi.c
 * @brief Unified multi-call entry point for OpenLogi Pure C.
 */

#include "subcommands.h"
#include "ipc_server.h"

#include <stdio.h>
#include <string.h>

static const char *get_basename(const char *path)
{
    if (!path) return "openlogi";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void print_unified_help(const char *progname)
{
    printf("OpenLogi Pure C Hardware Control & Daemon (openlogi) v%s\n", OPENLOGI_AGENT_VERSION);
    printf("Native, ultra-lightweight driver and CLI for Logitech peripherals\n\n");
    printf("Usage: %s <command> [arguments] [options]\n\n", progname);
    printf("Core Commands:\n");
    printf("  doctor                         Run end-to-end permissions, daemon, and hardware diagnostic wizard\n");
    printf("  daemon                         Run background HID++ driver (options: -d/--daemon, -v)\n");
    printf("  stop                           Stop running background daemon or service\n");
    printf("  restart                        Restart running background daemon or service\n");
    printf("  permissions [request|reveal]   Inspect or configure macOS Accessibility & Input Monitoring\n");
    printf("  uninstall [--dry-run]          Remove this installation; keep configuration\n");
    printf("  service [status|enable|disable|start|restart|stop] Manage login autostart service (launchd/systemd)\n");
    printf("  discover, scan                 Direct hardware diagnostic inspection and register probe\n");
    printf("  status                         Show daemon health, uptime, permissions, and active app\n");
    printf("  list, devices                  List connected Logitech devices, battery, and sensor state\n");
    printf("  dpi [get|set <value>]          Inspect or adjust active sensor DPI in real time\n");
    printf("  smartshift [get|set <m> <t>]   Configure SmartShift wheel (ratchet/freespin/auto)\n");
    printf("  scroll [invert|hires|smooth]   Configure mouse wheel scroll direction & resolution\n");
    printf("  config [show|path|get|set]     Inspect or modify config.toml settings\n");
    printf("  reload                         Hot-reload config.toml immediately\n");
    printf("  monitor                        Stream live intercepted events and gestures\n");
    printf("  setup                          Launch interactive ANSI terminal setup wizard\n");
    printf("  actions [filter]               List all 53 supported button & gesture actions\n\n");
    printf("Options:\n");
    printf("  --json                         Output in machine-readable JSON format\n");
    printf("  -s, --socket <path>            Specify custom UNIX domain socket path\n");
    printf("  -a, --autospawn                Automatically start daemon if not running\n");
    printf("  -v, --version                  Display version information\n");
    printf("  -h, --help                     Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s status                      # Check daemon health & connected hardware\n", progname);
    printf("  %s discover                    # Scan raw USB/BLE endpoints for Logitech devices\n", progname);
    printf("  %s service enable              # Install and start automatic login service\n", progname);
    printf("  %s scroll invert toggle        # Invert mouse scroll direction\n", progname);
    printf("  %s dpi set 1600                # Set sensor DPI\n", progname);
}

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "single_instance.h"
#include "ipc_client.h"
#include "yyjson.h"

static void macos_prompt_app_permissions(void)
{
    bool acc = AXIsProcessTrusted();
    bool inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);

    if (!acc) {
        const void *keys[] = { kAXTrustedCheckOptionPrompt };
        const void *values[] = { kCFBooleanTrue };
        CFDictionaryRef options = CFDictionaryCreate(
            kCFAllocatorDefault,
            keys,
            values,
            1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks
        );
        if (options) {
            AXIsProcessTrustedWithOptions(options);
            CFRelease(options);
        }
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility\" 2>/dev/null");
    }
    if (!inp) {
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
        CGRequestListenEventAccess();
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent\" 2>/dev/null");
    }

    if (!acc || !inp) {
        (void)system("osascript -e 'display alert \"OpenLogi Setup Required\" message \"OpenLogi requires Accessibility and Input Monitoring permissions in System Settings to control your Logitech mouse.\n\nPlease toggle ON OpenLogi in the System Settings window that opened.\" as informational buttons {\"OK\"} default button \"OK\"' 2>/dev/null &");
    }
}

static void macos_ensure_autostart_configured(const char *arg0)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;

    char plist_path[512];
    snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);

    /* If plist already exists, autostart is already configured */
    if (access(plist_path, F_OK) == 0) {
        return;
    }

    /* Determine binary path */
    char bin_path[1024];
    if (arg0 && strstr(arg0, ".app/Contents/MacOS/")) {
        strncpy(bin_path, arg0, sizeof(bin_path) - 1);
        bin_path[sizeof(bin_path) - 1] = '\0';
    } else if (access("/Applications/OpenLogi.app/Contents/MacOS/openlogi", X_OK) == 0) {
        strncpy(bin_path, "/Applications/OpenLogi.app/Contents/MacOS/openlogi", sizeof(bin_path) - 1);
        bin_path[sizeof(bin_path) - 1] = '\0';
    } else {
        snprintf(bin_path, sizeof(bin_path), "%s/Applications/OpenLogi.app/Contents/MacOS/openlogi", home);
        if (access(bin_path, X_OK) != 0) {
            return;
        }
    }

    char agents_dir[512];
    snprintf(agents_dir, sizeof(agents_dir), "%s/Library/LaunchAgents", home);
    mkdir(agents_dir, 0755);

    FILE *f = fopen(plist_path, "w");
    if (!f) return;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    fprintf(f, "<plist version=\"1.0\">\n<dict>\n");
    fprintf(f, "    <key>Label</key>\n    <string>io.github.openlogi.agent</string>\n");
    fprintf(f, "    <key>ProgramArguments</key>\n    <array>\n");
    fprintf(f, "        <string>%s</string>\n", bin_path);
    fprintf(f, "        <string>daemon</string>\n");
    fprintf(f, "    </array>\n");
    fprintf(f, "    <key>RunAtLoad</key>\n    <true/>\n");
    fprintf(f, "    <key>KeepAlive</key>\n");
    fprintf(f, "    <dict>\n");
    fprintf(f, "        <key>SuccessfulExit</key>\n");
    fprintf(f, "        <false/>\n");
    fprintf(f, "    </dict>\n");
    fprintf(f, "    <key>ProcessType</key>\n    <string>Interactive</string>\n");
    fprintf(f, "    <key>StandardOutPath</key>\n    <string>/tmp/openlogid.stdout.log</string>\n");
    fprintf(f, "    <key>StandardErrorPath</key>\n    <string>/tmp/openlogid.stderr.log</string>\n");
    fprintf(f, "</dict>\n</plist>\n");
    fclose(f);
}

static int handle_app_bundle_launch(int argc, char **argv)
{
    /* 1. Check if an instance of OpenLogi is already running */
    pid_t existing_pid = 0;
    int lock_fd = single_instance_acquire(NULL, &existing_pid);

    if (lock_fd < 0 && existing_pid > 0) {
        /* Already running! Query status via IPC client to get device info */
        ipc_client_t client;
        char device_info[256] = "Logitech Device (Connected)";
        bool has_client = ipc_client_connect(NULL, &client);

        if (has_client) {
            yyjson_doc *doc = ipc_client_call(&client, "openlogi.get_inventory", "{}");
            if (doc) {
                yyjson_val *root = yyjson_doc_get_root(doc);
                yyjson_val *res = yyjson_obj_get(root, "result");
                yyjson_val *devs = yyjson_obj_get(res, "devices");
                if (yyjson_is_arr(devs) && yyjson_arr_size(devs) > 0) {
                    yyjson_val *dev0 = yyjson_arr_get(devs, 0);
                    const char *name = yyjson_get_str(yyjson_obj_get(dev0, "product_name"));
                    uint32_t batt = (uint32_t)yyjson_get_uint(yyjson_obj_get(dev0, "battery_percent"));
                    if (name) {
                        snprintf(device_info, sizeof(device_info), "%s (Battery: %u%%)", name, (unsigned)batt);
                    }
                }
                yyjson_doc_free(doc);
            }
            ipc_client_close(&client);
        }

        /* Show native macOS alert dialog with options */
        char prompt_cmd[1024];
        snprintf(prompt_cmd, sizeof(prompt_cmd),
            "osascript -e 'set res to button returned of (display alert \"OpenLogi is Running\" "
            "message \"OpenLogi is active in the background (PID: %d).\n\n"
            "Device: %s\n\n"
            "What would you like to do?\" "
            "buttons {\"Keep Running\", \"Stop OpenLogi\", \"Uninstall\"} "
            "default button \"Keep Running\")\n"
            "return res' 2>/dev/null",
            (int)existing_pid, device_info);

        FILE *fp = popen(prompt_cmd, "r");
        char out[64] = { 0 };
        if (fp) {
            if (fgets(out, sizeof(out), fp)) {
                char *nl = strchr(out, '\n');
                if (nl) *nl = '\0';
                char *cr = strchr(out, '\r');
                if (cr) *cr = '\0';
            }
            pclose(fp);
        }

        const char *home = getenv("HOME");
        char plist_path[512] = { 0 };
        if (home) {
            snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);
        }

        if (strcmp(out, "Stop OpenLogi") == 0) {
            /* Unload from launchctl so launchd doesn't restart it */
            char target[128];
            snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
            char bootout[256];
            snprintf(bootout, sizeof(bootout), "/bin/launchctl bootout %s 2>/dev/null", target);
            (void)system(bootout);

            kill(existing_pid, SIGTERM);
            (void)system("osascript -e 'display notification \"OpenLogi stopped and restored mouse hardware.\" with title \"OpenLogi\"' 2>/dev/null &");
        } else if (strcmp(out, "Uninstall") == 0) {
            /* Full clean uninstall */
            char target[128];
            snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
            char bootout[256];
            snprintf(bootout, sizeof(bootout), "/bin/launchctl bootout %s 2>/dev/null", target);
            (void)system(bootout);

            if (plist_path[0]) unlink(plist_path);

            kill(existing_pid, SIGTERM);

            /* Clean locks and sockets */
            if (home) {
                char sock_path[512], lock_path[512];
                snprintf(sock_path, sizeof(sock_path), "%s/.local/state/openlogi/openlogi.sock", home);
                snprintf(lock_path, sizeof(lock_path), "%s/.local/state/openlogi/openlogi.lock", home);
                unlink(sock_path);
                unlink(lock_path);
            }

            /* Move app to Trash */
            char app_path[1024] = { 0 };
            if (access("/Applications/OpenLogi.app", F_OK) == 0) {
                strncpy(app_path, "/Applications/OpenLogi.app", sizeof(app_path) - 1);
            } else if (home) {
                snprintf(app_path, sizeof(app_path), "%s/Applications/OpenLogi.app", home);
            }

            if (app_path[0] && access(app_path, F_OK) == 0) {
                char trash_cmd[1200];
                snprintf(trash_cmd, sizeof(trash_cmd),
                    "osascript -e 'tell application \"Finder\" to delete POSIX file \"%s\"' 2>/dev/null",
                    app_path);
                (void)system(trash_cmd);
            }

            (void)system("osascript -e 'display alert \"OpenLogi Uninstalled\" message \"OpenLogi has been cleanly uninstalled, startup services removed, and the application moved to Trash.\" buttons {\"OK\"} default button \"OK\"' 2>/dev/null &");
        }
        return 0;
    }

    /* OpenLogi is not currently running. Release the lock descriptor so daemon_main can claim it. */
    if (lock_fd >= 0) {
        single_instance_release(lock_fd, NULL);
    }

    /* 2. Check and request macOS permissions */
    macos_prompt_app_permissions();

    /* 3. Ensure LaunchAgent autostart is written */
    macos_ensure_autostart_configured(argv[0]);

    /* 4. Launch the daemon */
    return daemon_main(argc, argv);
}
#endif

int main(int argc, char **argv)
{
    const char *prog = get_basename(argv[0]);

    /* 1. Legacy multi-call symlink detection */
    if (strcmp(prog, "openlogid") == 0) {
        return daemon_main(argc, argv);
    }
    if (strcmp(prog, "openlogi_discover") == 0) {
        return discover_main(argc, argv);
    }
    if (strcmp(prog, "openlogictl") == 0) {
        return ctl_main(argc, argv);
    }

#ifdef __APPLE__
    /* Check if launched by Finder/Spotlight/Launchpad as a macOS .app bundle */
    bool is_app_bundle = (strstr(argv[0], ".app/Contents/MacOS/") != NULL);
    bool is_psn_launch = (argc >= 2 && strncmp(argv[1], "-psn", 4) == 0);

    if (is_app_bundle || is_psn_launch) {
        return handle_app_bundle_launch(argc, argv);
    }
#endif

    /* 2. Direct invocation as 'openlogi' */
    if (argc < 2) {
        print_unified_help(prog);
        return 0;
    }

    const char *first_arg = argv[1];

    if (strcmp(first_arg, "-h") == 0 || strcmp(first_arg, "--help") == 0 || strcmp(first_arg, "help") == 0) {
        print_unified_help(prog);
        return 0;
    }

    if (strcmp(first_arg, "-v") == 0 || strcmp(first_arg, "--version") == 0 || strcmp(first_arg, "version") == 0) {
        printf("OpenLogi Pure C (openlogi) v%s\n", OPENLOGI_AGENT_VERSION);
        return 0;
    }

    /* Daemon subcommand: openlogi daemon [options] */
    if (strcmp(first_arg, "daemon") == 0 || strcmp(first_arg, "serve") == 0 || strcmp(first_arg, "start") == 0) {
        return daemon_main(argc - 1, &argv[1]);
    }

    /* Discovery scanner subcommand: openlogi discover [options] */
    if (strcmp(first_arg, "discover") == 0 || strcmp(first_arg, "scan") == 0 || strcmp(first_arg, "probe") == 0) {
        return discover_main(argc - 1, &argv[1]);
    }

    /* All control subcommands (status, dpi, smartshift, scroll, service, setup, etc.) */
    return ctl_main(argc, argv);
}
