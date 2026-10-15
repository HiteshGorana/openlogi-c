/**
 * @file openlogi.c
 * @brief Unified multi-call entry point for OpenLogi Pure C.
 */

#include "subcommands.h"
#include "ipc_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>

#include "single_instance.h"
#include "ipc_client.h"
#include "yyjson.h"

static void get_executable_path(char *out, size_t sz)
{
    uint32_t buf_sz = (uint32_t)sz;
    if (_NSGetExecutablePath(out, &buf_sz) != 0) {
        out[0] = '\0';
    } else {
        char resolved[1024];
        if (realpath(out, resolved)) {
            strncpy(out, resolved, sz - 1);
            out[sz - 1] = '\0';
        }
    }
}

static bool is_running_from_dmg_or_translocation(const char *path)
{
    if (!path || !path[0]) return false;
    if (strncmp(path, "/Volumes/", 9) == 0) return true;
    if (strstr(path, "/AppTranslocation/") != NULL) return true;

    char resolved[1024];
    if (realpath(path, resolved)) {
        if (strncmp(resolved, "/Volumes/", 9) == 0) return true;
        if (strstr(resolved, "/AppTranslocation/") != NULL) return true;
    }
    return false;
}

static int handle_dmg_launch(void)
{
    const char *home = getenv("HOME");
    char user_app[512] = { 0 };
    if (home) {
        snprintf(user_app, sizeof(user_app), "%s/Applications/OpenLogi.app", home);
    }
    bool installed_exists = (access("/Applications/OpenLogi.app", F_OK) == 0) ||
                            (user_app[0] && access(user_app, F_OK) == 0);

    char script[1024];
    if (installed_exists) {
        snprintf(script, sizeof(script),
            "osascript -e 'set res to button returned of (display alert \"Install OpenLogi to Continue\" "
            "message \"OpenLogi is currently running from a disk image or temporary folder, but an installed copy was found in your Applications folder.\n\n"
            "Please open OpenLogi from Applications to continue.\" "
            "buttons {\"Open Applications\", \"Quit\"} default button \"Open Applications\")\nreturn res' 2>/dev/null");
    } else {
        snprintf(script, sizeof(script),
            "osascript -e 'set res to button returned of (display alert \"Install OpenLogi to Continue\" "
            "message \"OpenLogi cannot run directly from a disk image.\n\n"
            "Please drag OpenLogi to your Applications folder and open it from there.\" "
            "buttons {\"Open Applications\", \"Quit\"} default button \"Open Applications\")\nreturn res' 2>/dev/null");
    }

    FILE *fp = popen(script, "r");
    char btn[64] = { 0 };
    if (fp) {
        if (fgets(btn, sizeof(btn), fp)) {
            char *nl = strchr(btn, '\n'); if (nl) *nl = '\0';
            char *cr = strchr(btn, '\r'); if (cr) *cr = '\0';
        }
        pclose(fp);
    }

    if (strcmp(btn, "Open Applications") == 0) {
        if (installed_exists) {
            (void)system("open /Applications/OpenLogi.app 2>/dev/null || open ~/Applications/OpenLogi.app 2>/dev/null || open /Applications &");
        } else {
            (void)system("open /Applications &");
        }
    }
    return 0;
}

static void get_canonical_bin_path(const char *arg0, char *out, size_t out_sz)
{
    const char *home = getenv("HOME");
    if (access("/Applications/OpenLogi.app/Contents/MacOS/openlogi", X_OK) == 0) {
        strncpy(out, "/Applications/OpenLogi.app/Contents/MacOS/openlogi", out_sz - 1);
        out[out_sz - 1] = '\0';
    } else if (home) {
        snprintf(out, out_sz, "%s/Applications/OpenLogi.app/Contents/MacOS/openlogi", home);
        if (access(out, X_OK) != 0 && arg0) {
            strncpy(out, arg0, out_sz - 1);
            out[out_sz - 1] = '\0';
        }
    } else if (arg0) {
        strncpy(out, arg0, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

static void macos_configure_autostart(const char *arg0, bool enable)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;

    char plist_path[512];
    snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);

    char target[128];
    snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
    char bootout[256];
    snprintf(bootout, sizeof(bootout), "/bin/launchctl bootout %s 2>/dev/null", target);
    (void)system(bootout);

    if (!enable) {
        unlink(plist_path);
        return;
    }

    char bin_path[1024] = { 0 };
    get_canonical_bin_path(arg0, bin_path, sizeof(bin_path));
    if (!bin_path[0]) return;

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

    char bootstrap[512];
    snprintf(bootstrap, sizeof(bootstrap), "/bin/launchctl bootstrap gui/%u \"%s\" 2>/dev/null", (unsigned)getuid(), plist_path);
    (void)system(bootstrap);
}

static void get_onboarding_state_file(char *buf, size_t sz)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    snprintf(buf, sz, "%s/.local/state/openlogi/onboarding.state", home);
}

static void ensure_openlogi_state_dir(void)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.local", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/state", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/state/openlogi", home);
    mkdir(dir, 0755);
}

static bool load_onboarding_state(bool *completed, char *step, size_t step_sz)
{
    if (completed) *completed = false;
    if (step && step_sz > 0) step[0] = '\0';

    char path[512];
    get_onboarding_state_file(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "completed=", 10) == 0) {
            if (completed) *completed = (atoi(line + 10) != 0);
        } else if (strncmp(line, "step=", 5) == 0) {
            if (step && step_sz > 0) {
                strncpy(step, line + 5, step_sz - 1);
                step[step_sz - 1] = '\0';
                char *nl = strchr(step, '\n'); if (nl) *nl = '\0';
                char *cr = strchr(step, '\r'); if (cr) *cr = '\0';
            }
        }
    }
    fclose(f);
    return true;
}

static void save_onboarding_state(bool completed, const char *step)
{
    ensure_openlogi_state_dir();
    char path[512];
    get_onboarding_state_file(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "completed=%d\nstep=%s\n", completed ? 1 : 0, step ? step : "welcome");
    fclose(f);
}

static void run_alert(const char *title, const char *msg, const char *buttons, const char *default_btn, char *out_btn, size_t out_sz)
{
    char script[2048];
    snprintf(script, sizeof(script),
        "osascript -e 'set res to button returned of (display alert \"%s\" message \"%s\" buttons %s default button \"%s\")\nreturn res' 2>/dev/null",
        title, msg, buttons, default_btn);
    FILE *fp = popen(script, "r");
    if (out_btn && out_sz > 0) out_btn[0] = '\0';
    if (fp) {
        if (out_btn && out_sz > 0 && fgets(out_btn, (int)out_sz, fp)) {
            char *nl = strchr(out_btn, '\n'); if (nl) *nl = '\0';
            char *cr = strchr(out_btn, '\r'); if (cr) *cr = '\0';
        }
        pclose(fp);
    }
}

static bool ensure_daemon_running(const char *arg0)
{
    ipc_client_t client;
    if (ipc_client_connect(NULL, &client)) {
        ipc_client_close(&client);
        return true;
    }

    char bin_path[1024] = { 0 };
    get_canonical_bin_path(arg0, bin_path, sizeof(bin_path));
    if (!bin_path[0]) return false;

    char spawn_cmd[1200];
    snprintf(spawn_cmd, sizeof(spawn_cmd), "\"%s\" daemon -d 2>/dev/null &", bin_path);
    (void)system(spawn_cmd);

    for (int i = 0; i < 20; i++) {
        usleep(100000);
        if (ipc_client_connect(NULL, &client)) {
            ipc_client_close(&client);
            return true;
        }
    }
    return false;
}

static int handle_already_running(pid_t existing_pid)
{
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

    char prompt_msg[512];
    snprintf(prompt_msg, sizeof(prompt_msg),
        "OpenLogi is active in the background (PID: %d).\n\nDevice: %s\n\nWhat would you like to do?",
        (int)existing_pid, device_info);

    char out[64] = { 0 };
    run_alert("OpenLogi is Running", prompt_msg,
              "{\"Keep Running\", \"Stop OpenLogi\", \"Uninstall\"}", "Keep Running", out, sizeof(out));

    const char *home = getenv("HOME");
    char plist_path[512] = { 0 };
    if (home) {
        snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);
    }

    if (strcmp(out, "Stop OpenLogi") == 0) {
        char target[128];
        snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
        char bootout[256];
        snprintf(bootout, sizeof(bootout), "/bin/launchctl bootout %s 2>/dev/null", target);
        (void)system(bootout);

        kill(existing_pid, SIGTERM);
        (void)system("osascript -e 'display notification \"OpenLogi stopped and restored mouse hardware.\" with title \"OpenLogi\"' 2>/dev/null &");
    } else if (strcmp(out, "Uninstall") == 0) {
        char target[128];
        snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
        char bootout[256];
        snprintf(bootout, sizeof(bootout), "/bin/launchctl bootout %s 2>/dev/null", target);
        (void)system(bootout);

        if (plist_path[0]) unlink(plist_path);
        kill(existing_pid, SIGTERM);

        if (home) {
            char sock_path[512], lock_path[512];
            snprintf(sock_path, sizeof(sock_path), "%s/.local/state/openlogi/openlogi.sock", home);
            snprintf(lock_path, sizeof(lock_path), "%s/.local/state/openlogi/openlogi.lock", home);
            unlink(sock_path);
            unlink(lock_path);
        }

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

static int handle_app_bundle_launch(int argc, char **argv)
{
    (void)argc;

    /* 1. DMG / Translocation safety check */
    char exec_path[1024] = { 0 };
    get_executable_path(exec_path, sizeof(exec_path));
    if (is_running_from_dmg_or_translocation(exec_path) || is_running_from_dmg_or_translocation(argv[0])) {
        return handle_dmg_launch();
    }

    /* 2. Check if an instance of OpenLogi is already running */
    pid_t existing_pid = 0;
    int lock_fd = single_instance_acquire(NULL, &existing_pid);

    if (lock_fd < 0 && existing_pid > 0) {
        return handle_already_running(existing_pid);
    }

    if (lock_fd >= 0) {
        single_instance_release(lock_fd, NULL);
    }

    /* 3. Check resumable onboarding state */
    bool completed = false;
    char step[64] = { 0 };
    load_onboarding_state(&completed, step, sizeof(step));

    if (completed) {
        char btn[64] = { 0 };
        run_alert("OpenLogi", "OpenLogi is currently stopped.\n\nWhat would you like to do?",
                  "{\"Start OpenLogi\", \"Re-run Setup\", \"Quit\"}", "Start OpenLogi", btn, sizeof(btn));
        if (strcmp(btn, "Start OpenLogi") == 0) {
            ensure_daemon_running(argv[0]);
            (void)system("osascript -e 'display notification \"OpenLogi driver is now running in background.\" with title \"OpenLogi\"' 2>/dev/null &");
            return 0;
        } else if (strcmp(btn, "Re-run Setup") == 0) {
            completed = false;
            step[0] = '\0';
        } else {
            return 0;
        }
    }

    /* Step 1: Welcome */
    if (step[0] == '\0' || strcmp(step, "welcome") == 0) {
        char btn[64] = { 0 };
        run_alert("Set Up Your Logitech Mouse",
                  "OpenLogi customizes your mouse buttons and scrolling.\n\nWe will check permissions, find your mouse, and test a button before applying settings.",
                  "{\"Set Up My Mouse\", \"Finish Later\"}", "Set Up My Mouse", btn, sizeof(btn));
        if (strcmp(btn, "Finish Later") == 0) {
            save_onboarding_state(false, "welcome");
            return 0;
        }
        save_onboarding_state(false, "input_monitoring");
        strncpy(step, "input_monitoring", sizeof(step));
    }

    /* Step 2: Input Monitoring */
    while (true) {
        bool inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);
        if (inp) break;

        char btn[64] = { 0 };
        run_alert("Allow OpenLogi to Receive Mouse Input",
                  "Step 1 of 4: Input Monitoring\n\nOpenLogi uses Input Monitoring to receive input from mouse buttons and scrolling.\n\nmacOS describes this permission broadly because it allows monitoring keyboard, mouse, and trackpad events.\n\nClick 'Enable Input Monitoring' to open System Settings, toggle OpenLogi ON, and return here.",
                  "{\"Enable Input Monitoring\", \"Check Again\", \"Finish Later\"}", "Enable Input Monitoring", btn, sizeof(btn));
        if (strcmp(btn, "Finish Later") == 0) {
            save_onboarding_state(false, "input_monitoring");
            return 0;
        }
        if (strcmp(btn, "Enable Input Monitoring") == 0) {
            IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
            CGRequestListenEventAccess();
            (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent\" 2>/dev/null");
        }
        for (int i = 0; i < 30; i++) {
            usleep(500000);
            if (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted) {
                inp = true;
                break;
            }
        }
        if (inp) break;
    }
    save_onboarding_state(false, "accessibility");
    strncpy(step, "accessibility", sizeof(step));

    /* Step 3: Accessibility */
    while (true) {
        bool acc = AXIsProcessTrusted();
        if (acc) break;

        char btn[64] = { 0 };
        run_alert("Allow Mouse Buttons to Perform Actions",
                  "Step 2 of 4: Accessibility\n\nOpenLogi uses Accessibility access for customized actions such as shortcuts and window navigation.\n\nClick 'Enable Accessibility' to open System Settings, toggle OpenLogi ON, and return here.",
                  "{\"Enable Accessibility\", \"Check Again\", \"Finish Later\"}", "Enable Accessibility", btn, sizeof(btn));
        if (strcmp(btn, "Finish Later") == 0) {
            save_onboarding_state(false, "accessibility");
            return 0;
        }
        if (strcmp(btn, "Enable Accessibility") == 0) {
            const void *keys[] = { kAXTrustedCheckOptionPrompt };
            const void *values[] = { kCFBooleanTrue };
            CFDictionaryRef options = CFDictionaryCreate(
                kCFAllocatorDefault, keys, values, 1,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
            );
            if (options) {
                AXIsProcessTrustedWithOptions(options);
                CFRelease(options);
            }
            (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility\" 2>/dev/null");
        }
        for (int i = 0; i < 30; i++) {
            usleep(500000);
            if (AXIsProcessTrusted()) {
                acc = true;
                break;
            }
        }
        if (acc) break;
    }
    save_onboarding_state(false, "mouse");
    strncpy(step, "mouse", sizeof(step));

    /* Step 4: Find and Select Mouse */
    ensure_daemon_running(argv[0]);

    char mouse_info[256] = { 0 };
    bool found_mouse = false;
    ipc_client_t client;
    if (ipc_client_connect(NULL, &client)) {
        yyjson_doc *inv = ipc_client_call(&client, "openlogi.get_inventory", "{}");
        if (inv) {
            yyjson_val *root = yyjson_doc_get_root(inv);
            yyjson_val *res = yyjson_obj_get(root, "result");
            yyjson_val *devs = yyjson_obj_get(res, "devices");
            if (yyjson_is_arr(devs) && yyjson_arr_size(devs) > 0) {
                yyjson_val *d0 = yyjson_arr_get(devs, 0);
                const char *pname = yyjson_get_str(yyjson_obj_get(d0, "product_name"));
                uint32_t bval = (uint32_t)yyjson_get_uint(yyjson_obj_get(d0, "battery_percent"));
                snprintf(mouse_info, sizeof(mouse_info), "%s (Battery: %u%%)", pname ? pname : "Logitech Mouse", (unsigned)bval);
                found_mouse = true;
            }
            yyjson_doc_free(inv);
        }
        ipc_client_close(&client);
    }

    if (found_mouse) {
        char btn[64] = { 0 };
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Step 3 of 4: Mouse Detected\n\nFound: %s\n\nYour mouse is communicating with OpenLogi.",
                 mouse_info);
        run_alert("Logitech Mouse Detected", msg, "{\"Continue\", \"Finish Later\"}", "Continue", btn, sizeof(btn));
        if (strcmp(btn, "Finish Later") == 0) {
            save_onboarding_state(false, "mouse");
            return 0;
        }
    } else {
        char btn[64] = { 0 };
        run_alert("Waiting for Your Mouse",
                  "Step 3 of 4: Connect Mouse\n\nTurn on your Logitech mouse and connect it via Bluetooth or its USB receiver.\n\nOpenLogi will automatically detect it once connected.",
                  "{\"Check Again\", \"Finish Later\"}", "Check Again", btn, sizeof(btn));
        if (strcmp(btn, "Finish Later") == 0) {
            save_onboarding_state(false, "mouse");
            (void)system("osascript -e 'display alert \"Setup Saved\" message \"Setup progress saved. Connect your mouse anytime and re-open OpenLogi to finish.\" buttons {\"OK\"} default button \"OK\"' 2>/dev/null &");
            return 0;
        }
    }

    /* Step 5: Verify Interaction (Button Test) */
    char btn_test[64] = { 0 };
    run_alert("Test a Side Button",
              "Step 4 of 4: Verification\n\nClick any side button or gesture button on your mouse to verify that OpenLogi intercepts it, or click 'Skip Test'.",
              "{\"Test Side Button\", \"Skip Test\"}", "Test Side Button", btn_test, sizeof(btn_test));

    if (strcmp(btn_test, "Test Side Button") == 0) {
        ipc_client_t test_client;
        if (ipc_client_connect(NULL, &test_client)) {
            yyjson_doc *sub = ipc_client_call(&test_client, "openlogi.subscribe", "{}");
            if (sub) yyjson_doc_free(sub);
            bool heard = false;
            for (int i = 0; i < 40 && !heard; i++) {
                yyjson_doc *ev = ipc_client_read_event(&test_client, 100);
                if (ev) {
                    yyjson_val *r = yyjson_doc_get_root(ev);
                    yyjson_val *p = yyjson_obj_get(r, "params");
                    const char *ev_name = yyjson_get_str(yyjson_obj_get(p, "event"));
                    if (ev_name && (strcmp(ev_name, "gesture_triggered") == 0 ||
                                    strcmp(ev_name, "dpi_changed") == 0 ||
                                    strcmp(ev_name, "smartshift_changed") == 0 ||
                                    strcmp(ev_name, "button_down") == 0 ||
                                    strcmp(ev_name, "button_up") == 0)) {
                        heard = true;
                    }
                    yyjson_doc_free(ev);
                }
            }
            ipc_client_close(&test_client);

            if (heard) {
                run_alert("Button Verified!", "OpenLogi successfully intercepted the button click from your mouse.",
                          "{\"Continue\"}", "Continue", NULL, 0);
            } else {
                run_alert("No Click Detected", "No button event was detected during the test window. You can customize controls anytime.",
                          "{\"Continue\"}", "Continue", NULL, 0);
            }
        }
    }

    /* Step 6: Autostart Preference & Finish */
    char btn_auto[64] = { 0 };
    run_alert("OpenLogi Setup Complete",
              "Your Logitech mouse is now configured!\n\nWould you like OpenLogi to start automatically whenever you log in to your Mac?",
              "{\"Start at Login\", \"No, Start Manually\"}", "Start at Login", btn_auto, sizeof(btn_auto));

    if (strcmp(btn_auto, "Start at Login") == 0) {
        macos_configure_autostart(argv[0], true);
    } else {
        macos_configure_autostart(argv[0], false);
        ensure_daemon_running(argv[0]);
    }

    save_onboarding_state(true, "completed");

    run_alert("OpenLogi is Active",
              "Setup is complete! OpenLogi is running silently in the background.\n\nYou can customize buttons and gestures anytime in:\n~/.config/openlogi/config.toml",
              "{\"OK\"}", "OK", NULL, 0);

    return 0;
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

    /* 2. Check explicit CLI subcommands & flags FIRST */
    if (argc >= 2) {
        const char *first_arg = argv[1];

        /* macOS LaunchServices may pass -psn_... when launching an app bundle */
        if (strncmp(first_arg, "-psn", 4) == 0) {
#ifdef __APPLE__
            return handle_app_bundle_launch(argc, argv);
#else
            return 0;
#endif
        }

        if (strcmp(first_arg, "-h") == 0 || strcmp(first_arg, "--help") == 0 || strcmp(first_arg, "help") == 0) {
            print_unified_help(prog);
            return 0;
        }

        if (strcmp(first_arg, "-v") == 0 || strcmp(first_arg, "--version") == 0 || strcmp(first_arg, "version") == 0) {
            printf("OpenLogi Pure C (openlogi) v%s\n", OPENLOGI_AGENT_VERSION);
            return 0;
        }

        /* Daemon subcommand: openlogi daemon [options] - strictly noninteractive! */
        if (strcmp(first_arg, "daemon") == 0 || strcmp(first_arg, "serve") == 0 || strcmp(first_arg, "start") == 0) {
            return daemon_main(argc - 1, &argv[1]);
        }

        /* Discovery scanner subcommand: openlogi discover [options] */
        if (strcmp(first_arg, "discover") == 0 || strcmp(first_arg, "scan") == 0 || strcmp(first_arg, "probe") == 0) {
            return discover_main(argc - 1, &argv[1]);
        }

        /* Explicit onboard subcommand */
        if (strcmp(first_arg, "onboard") == 0) {
#ifdef __APPLE__
            return handle_app_bundle_launch(argc, argv);
#else
            return ctl_main(argc, argv);
#endif
        }

        /* All other CLI control subcommands (status, doctor, dpi, smartshift, scroll, service, setup, etc.) */
        return ctl_main(argc, argv);
    }

    /* 3. Invocation with argc == 1 (no arguments) */
#ifdef __APPLE__
    /* If run as an .app bundle or launched by Finder/GUI without a terminal, launch onboarding */
    if (strstr(argv[0], ".app/Contents/MacOS/") != NULL || isatty(STDIN_FILENO) == 0) {
        return handle_app_bundle_launch(argc, argv);
    }
#endif

    /* Otherwise, in a terminal with no arguments, print help */
    print_unified_help(prog);
    return 0;
}
