/**
 * @file openlogictl.c
 * @brief OpenLogi Pure C Command-Line Tool and Setup Wizard.
 */

#include "ipc_client.h"
#include "ipc_server.h"
#include "config.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RED     "\033[31m"

static bool g_json_output = false;
static bool g_autospawn = false;
static char g_custom_socket[512] = { 0 };

static void print_banner(void)
{
    if (g_json_output) return;
    printf(COLOR_BOLD COLOR_CYAN "=====================================================\n");
    printf(" OpenLogi Pure C Control Tool (openlogictl) v%s\n", OPENLOGI_AGENT_VERSION);
    printf("=====================================================" COLOR_RESET "\n");
}

static bool connect_or_warn(ipc_client_t *client)
{
    const char *sock = (g_custom_socket[0] != '\0') ? g_custom_socket : NULL;
    if (ipc_client_connect(sock, client)) {
        return true;
    }

    if (g_autospawn) {
        if (!g_json_output) {
            printf("[*] Daemon not running. Auto-spawning openlogid...\n");
        }
        (void)system("openlogid --daemon > /dev/null 2>&1 || bin/openlogid --daemon > /dev/null 2>&1");
        usleep(150000); /* 150ms settling time */
        if (ipc_client_connect(sock, client)) {
            return true;
        }
    }

    if (g_json_output) {
        printf("{\"error\":\"Daemon not running or unreachable\"}\n");
    } else {
        fprintf(stderr, COLOR_RED "[-] Error: Could not connect to OpenLogi daemon (openlogid)." COLOR_RESET "\n");
        fprintf(stderr, "    Please verify that openlogid is running:\n");
        fprintf(stderr, "      openlogid --daemon\n");
        fprintf(stderr, "    Or use --autospawn to launch automatically.\n\n");
    }
    return false;
}

/* --- Subcommand: status --- */
static int cmd_status(void)
{
    ipc_client_t client;
    if (!connect_or_warn(&client)) return 1;

    char *raw = ipc_client_call_raw(&client, "openlogi.get_status", "{}");
    ipc_client_close(&client);

    if (!raw) {
        fprintf(stderr, "[-] Failed to retrieve daemon status\n");
        return 1;
    }

    if (g_json_output) {
        printf("%s\n", raw);
        free(raw);
        return 0;
    }

    yyjson_doc *doc = yyjson_read(raw, strlen(raw), 0);
    free(raw);
    if (!doc) return 1;

    yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    if (!res) {
        yyjson_doc_free(doc);
        return 1;
    }

    print_banner();

    uint64_t pid = yyjson_get_uint(yyjson_obj_get(res, "pid"));
    uint64_t uptime = yyjson_get_uint(yyjson_obj_get(res, "uptime_sec"));
    uint64_t hrs = uptime / 3600;
    uint64_t mins = (uptime % 3600) / 60;
    uint64_t secs = uptime % 60;

    printf("Daemon State:        " COLOR_BOLD COLOR_GREEN "RUNNING" COLOR_RESET " (PID: %llu, Uptime: %lluh %llum %llus)\n",
           (unsigned long long)pid, (unsigned long long)hrs, (unsigned long long)mins, (unsigned long long)secs);
    printf("Protocol Version:    %s\n", yyjson_get_str(yyjson_obj_get(res, "protocol")));
    printf("Agent Version:       %s\n", yyjson_get_str(yyjson_obj_get(res, "version")));

    bool hook_inst = yyjson_get_bool(yyjson_obj_get(res, "hook_installed"));
    printf("OS Input Hook:       %s\n", hook_inst ? COLOR_GREEN "ACTIVE (Event Tap installed)" COLOR_RESET : COLOR_YELLOW "PASSIVE" COLOR_RESET);

    yyjson_val *perms = yyjson_obj_get(res, "permissions");
    if (perms) {
        bool acc = yyjson_get_bool(yyjson_obj_get(perms, "accessibility"));
        bool inp = yyjson_get_bool(yyjson_obj_get(perms, "input_monitoring"));
        printf("Permissions:         Accessibility: %s | Input Monitoring: %s\n",
               acc ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_RED "DENIED" COLOR_RESET,
               inp ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_RED "DENIED" COLOR_RESET);
    }

    const char *front = yyjson_get_str(yyjson_obj_get(res, "frontmost_app"));
    printf("Active Frontmost:    %s\n", (front && front[0] != '\0') ? front : "(none / desktop)");

    uint64_t dev_count = yyjson_get_uint(yyjson_obj_get(res, "devices_online"));
    printf("Connected Devices:   " COLOR_BOLD "%llu online" COLOR_RESET "\n", (unsigned long long)dev_count);
    printf(COLOR_CYAN "=====================================================" COLOR_RESET "\n\n");

    yyjson_doc_free(doc);
    return 0;
}

/* --- Subcommand: list / inventory --- */
static int cmd_list(void)
{
    ipc_client_t client;
    if (!connect_or_warn(&client)) return 1;

    char *raw = ipc_client_call_raw(&client, "openlogi.get_inventory", "{}");
    ipc_client_close(&client);

    if (!raw) {
        fprintf(stderr, "[-] Failed to retrieve device inventory\n");
        return 1;
    }

    if (g_json_output) {
        printf("%s\n", raw);
        free(raw);
        return 0;
    }

    yyjson_doc *doc = yyjson_read(raw, strlen(raw), 0);
    free(raw);
    if (!doc) return 1;

    yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    yyjson_val *devs = res ? yyjson_obj_get(res, "devices") : NULL;

    size_t count = (devs && yyjson_is_arr(devs)) ? yyjson_arr_size(devs) : 0;

    print_banner();
    printf("Connected Logitech Devices (%zu found):\n", count);
    printf("----------------------------------------------------------------------------------------\n");
    printf(COLOR_BOLD "%-3s %-20s %-16s %-10s %-8s %-6s %-14s %-8s" COLOR_RESET "\n",
           "#", "Device Key", "Product Name", "VID:PID", "Battery", "DPI", "SmartShift", "Status");
    printf("----------------------------------------------------------------------------------------\n");

    if (count == 0) {
        printf("    No Logitech HID++ devices currently connected or online.\n");
    } else {
        for (size_t i = 0; i < count; i++) {
            yyjson_val *d = yyjson_arr_get(devs, i);
            const char *key = yyjson_get_str(yyjson_obj_get(d, "device_key"));
            const char *name = yyjson_get_str(yyjson_obj_get(d, "product_name"));
            const char *vid = yyjson_get_str(yyjson_obj_get(d, "vendor_id"));
            const char *pid = yyjson_get_str(yyjson_obj_get(d, "product_id"));
            uint64_t bat = yyjson_get_uint(yyjson_obj_get(d, "battery_percent"));
            bool chg = yyjson_get_bool(yyjson_obj_get(d, "is_charging"));
            uint64_t dpi = yyjson_get_uint(yyjson_obj_get(d, "dpi"));
            uint64_t ss_mode = yyjson_get_uint(yyjson_obj_get(d, "smartshift_mode"));
            uint64_t ss_thresh = yyjson_get_uint(yyjson_obj_get(d, "smartshift_threshold"));
            bool online = yyjson_get_bool(yyjson_obj_get(d, "online"));

            char vidpid[32];
            snprintf(vidpid, sizeof(vidpid), "%s:%s", vid ? vid + 2 : "046D", pid ? pid + 2 : "????");

            char bat_str[32];
            snprintf(bat_str, sizeof(bat_str), "%llu%%%s", (unsigned long long)bat, chg ? "⚡" : "");

            char ss_str[32];
            const char *m_name = (ss_mode == 1) ? "Ratchet" : (ss_mode == 2) ? "Freespin" : (ss_mode == 3) ? "Auto" : "Off";
            snprintf(ss_str, sizeof(ss_str), "%s(%llu)", m_name, (unsigned long long)ss_thresh);

            printf("%-3zu %-20s %-16s %-10s %-8s %-6llu %-14s %s\n",
                   i + 1,
                   key ? key : "unknown",
                   name ? name : "Logitech Device",
                   vidpid,
                   bat_str,
                   (unsigned long long)dpi,
                   ss_str,
                   online ? COLOR_GREEN "Online" COLOR_RESET : COLOR_YELLOW "Offline" COLOR_RESET);
        }
    }

    printf("----------------------------------------------------------------------------------------\n\n");

    yyjson_doc_free(doc);
    return 0;
}

/* --- Subcommand: dpi --- */
static int cmd_dpi(int argc, char **argv)
{
    if (argc < 1) {
        printf("Usage: openlogictl dpi get [device_key]\n");
        printf("       openlogictl dpi set <value> [device_key]\n");
        return 1;
    }

    const char *action = argv[0];

    if (strcmp(action, "get") == 0) {
        return cmd_list();
    } else if (strcmp(action, "set") == 0) {
        if (argc < 2) {
            fprintf(stderr, "[-] Error: DPI value required. Example: openlogictl dpi set 1600\n");
            return 1;
        }

        int dpi_val = atoi(argv[1]);
        if (dpi_val < 200 || dpi_val > 32000) {
            fprintf(stderr, "[-] Error: DPI value must be between 200 and 32000\n");
            return 1;
        }

        const char *dev_key = (argc >= 3) ? argv[2] : "";

        char params[256];
        if (dev_key[0] != '\0') {
            snprintf(params, sizeof(params), "{\"dpi\":%d,\"device_key\":\"%s\"}", dpi_val, dev_key);
        } else {
            snprintf(params, sizeof(params), "{\"dpi\":%d}", dpi_val);
        }

        ipc_client_t client;
        if (!connect_or_warn(&client)) return 1;

        yyjson_doc *doc = ipc_client_call(&client, "openlogi.set_dpi", params);
        ipc_client_close(&client);

        if (!doc) {
            fprintf(stderr, "[-] Failed to set DPI\n");
            return 1;
        }

        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *res = yyjson_obj_get(root, "result");
        if (res && yyjson_get_bool(yyjson_obj_get(res, "success"))) {
            if (g_json_output) {
                char *json = yyjson_write(doc, 0, NULL);
                printf("%s\n", json);
                free(json);
            } else {
                printf(COLOR_GREEN "[+] Successfully updated sensor DPI to %d" COLOR_RESET "\n", dpi_val);
            }
            yyjson_doc_free(doc);
            return 0;
        } else {
            yyjson_val *err = yyjson_obj_get(root, "error");
            fprintf(stderr, COLOR_RED "[-] Failed to set DPI: %s" COLOR_RESET "\n",
                    err ? yyjson_get_str(yyjson_obj_get(err, "message")) : "Unknown error");
            yyjson_doc_free(doc);
            return 1;
        }
    } else {
        fprintf(stderr, "[-] Unknown dpi command '%s'. Use 'get' or 'set'.\n", action);
        return 1;
    }
}

/* --- Subcommand: smartshift --- */
static int cmd_smartshift(int argc, char **argv)
{
    if (argc < 1) {
        printf("Usage: openlogictl smartshift get [device_key]\n");
        printf("       openlogictl smartshift set <ratchet|freespin|auto> <threshold> [device_key]\n");
        return 1;
    }

    const char *action = argv[0];

    if (strcmp(action, "get") == 0) {
        return cmd_list();
    } else if (strcmp(action, "set") == 0) {
        if (argc < 2) {
            fprintf(stderr, "[-] Error: Mode required (ratchet, freespin, auto)\n");
            return 1;
        }

        const char *mode = argv[1];
        int threshold = (argc >= 3) ? atoi(argv[2]) : 16;
        const char *dev_key = (argc >= 4) ? argv[3] : "";

        char params[256];
        if (dev_key[0] != '\0') {
            snprintf(params, sizeof(params), "{\"mode\":\"%s\",\"threshold\":%d,\"device_key\":\"%s\"}",
                     mode, threshold, dev_key);
        } else {
            snprintf(params, sizeof(params), "{\"mode\":\"%s\",\"threshold\":%d}", mode, threshold);
        }

        ipc_client_t client;
        if (!connect_or_warn(&client)) return 1;

        yyjson_doc *doc = ipc_client_call(&client, "openlogi.set_smartshift", params);
        ipc_client_close(&client);

        if (!doc) {
            fprintf(stderr, "[-] Failed to set SmartShift\n");
            return 1;
        }

        yyjson_val *root = yyjson_doc_get_root(doc);
        yyjson_val *res = yyjson_obj_get(root, "result");
        if (res && yyjson_get_bool(yyjson_obj_get(res, "success"))) {
            if (g_json_output) {
                char *json = yyjson_write(doc, 0, NULL);
                printf("%s\n", json);
                free(json);
            } else {
                printf(COLOR_GREEN "[+] Successfully updated SmartShift: mode='%s', threshold=%d" COLOR_RESET "\n",
                       mode, threshold);
            }
            yyjson_doc_free(doc);
            return 0;
        } else {
            yyjson_val *err = yyjson_obj_get(root, "error");
            fprintf(stderr, COLOR_RED "[-] Failed to configure SmartShift: %s" COLOR_RESET "\n",
                    err ? yyjson_get_str(yyjson_obj_get(err, "message")) : "Unknown error");
            yyjson_doc_free(doc);
            return 1;
        }
    } else {
        fprintf(stderr, "[-] Unknown smartshift command '%s'. Use 'get' or 'set'.\n", action);
        return 1;
    }
}

/* --- Subcommand: reload --- */
static int cmd_reload(void)
{
    ipc_client_t client;
    if (!connect_or_warn(&client)) return 1;

    yyjson_doc *doc = ipc_client_call(&client, "openlogi.reload_config", "{}");
    ipc_client_close(&client);

    if (!doc) {
        fprintf(stderr, "[-] Failed to send reload request\n");
        return 1;
    }

    yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    if (res && yyjson_get_bool(yyjson_obj_get(res, "success"))) {
        if (g_json_output) {
            printf("{\"success\":true}\n");
        } else {
            printf(COLOR_GREEN "[+] OpenLogi daemon reloaded configuration successfully." COLOR_RESET "\n");
        }
        yyjson_doc_free(doc);
        return 0;
    } else {
        fprintf(stderr, COLOR_RED "[-] Reload command failed." COLOR_RESET "\n");
        yyjson_doc_free(doc);
        return 1;
    }
}

/* --- Subcommand: monitor --- */
static volatile bool g_monitor_running = true;

static void on_sigint_monitor(int sig)
{
    (void)sig;
    g_monitor_running = false;
}

static int cmd_monitor(void)
{
    ipc_client_t client;
    if (!connect_or_warn(&client)) return 1;

    /* Subscribe */
    yyjson_doc *sub_doc = ipc_client_call(&client, "openlogi.subscribe", "{}");
    if (!sub_doc) {
        fprintf(stderr, "[-] Failed to subscribe to daemon event stream\n");
        ipc_client_close(&client);
        return 1;
    }
    yyjson_doc_free(sub_doc);

    signal(SIGINT, on_sigint_monitor);
    signal(SIGTERM, on_sigint_monitor);

    if (!g_json_output) {
        print_banner();
        printf("Connected to OpenLogi real-time event stream. Press " COLOR_BOLD "Ctrl+C" COLOR_RESET " to exit.\n\n");
    }

    while (g_monitor_running) {
        yyjson_doc *ev_doc = ipc_client_read_event(&client, 200);
        if (!ev_doc) continue;

        yyjson_val *root = yyjson_doc_get_root(ev_doc);
        const char *method = yyjson_get_str(yyjson_obj_get(root, "method"));
        if (method && strcmp(method, "openlogi.event") == 0) {
            yyjson_val *params = yyjson_obj_get(root, "params");
            const char *ev_name = yyjson_get_str(yyjson_obj_get(params, "event"));

            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char tstr[32];
            strftime(tstr, sizeof(tstr), "%H:%M:%S", t);

            if (g_json_output) {
                char *json = yyjson_write(ev_doc, 0, NULL);
                printf("%s\n", json);
                free(json);
            } else {
                printf("[%s] " COLOR_BOLD COLOR_GREEN "EVENT: %-16s" COLOR_RESET " ", tstr, ev_name ? ev_name : "unknown");
                yyjson_val *data = yyjson_obj_get(params, "data");
                if (data) {
                    char *d_str = yyjson_val_write(data, 0, NULL);
                    printf("%s\n", d_str ? d_str : "");
                    free(d_str);
                } else {
                    printf("\n");
                }
            }
            fflush(stdout);
        }
        yyjson_doc_free(ev_doc);
    }

    ipc_client_close(&client);
    if (!g_json_output) {
        printf("\nDisconnected from event stream.\n");
    }
    return 0;
}

/* --- Subcommand: setup (Interactive Terminal Wizard) --- */
static int cmd_setup(void)
{
    print_banner();
    printf(COLOR_BOLD "Interactive Device Setup Wizard" COLOR_RESET "\n\n");

    ipc_client_t client;
    if (!connect_or_warn(&client)) {
        printf("OpenLogi daemon must be running to configure devices.\n");
        return 1;
    }

    while (1) {
        printf("\n" COLOR_BOLD "Main Configuration Menu:" COLOR_RESET "\n");
        printf("  [1] View Connected Devices & Battery\n");
        printf("  [2] Adjust Sensor DPI Resolution\n");
        printf("  [3] Configure SmartShift Scroll Wheel\n");
        printf("  [4] Reload Configuration from Disk\n");
        printf("  [5] Live Input & Gesture Monitor\n");
        printf("  [0] Exit Wizard\n");
        printf(COLOR_CYAN "Select option (0-5): " COLOR_RESET);
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) break;

        int choice = atoi(line);
        if (choice == 0) {
            printf("\nExiting setup wizard. Happy clicking!\n");
            break;
        }

        switch (choice) {
            case 1:
                cmd_list();
                break;
            case 2: {
                printf("\nEnter new sensor DPI resolution (e.g. 800, 1200, 1600, 2400, 4000): ");
                fflush(stdout);
                if (fgets(line, sizeof(line), stdin)) {
                    int dpi = atoi(line);
                    if (dpi >= 200 && dpi <= 32000) {
                        char *args[] = { (char *)"set", line, NULL };
                        cmd_dpi(2, args);
                    } else {
                        printf(COLOR_RED "Invalid DPI resolution.\n" COLOR_RESET);
                    }
                }
                break;
            }
            case 3: {
                printf("\nSmartShift Modes:\n");
                printf("  [1] Ratchet (Clicky tactile scrolling)\n");
                printf("  [2] Freespin (Hyper-fast frictionless spinning)\n");
                printf("  [3] Auto (SmartShift auto-switch on flick)\n");
                printf("Select mode (1-3): ");
                fflush(stdout);
                if (fgets(line, sizeof(line), stdin)) {
                    int m = atoi(line);
                    const char *m_str = (m == 1) ? "ratchet" : (m == 2) ? "freespin" : "auto";
                    printf("Enter flick torque threshold (1-50, default 16): ");
                    fflush(stdout);
                    char t_line[32];
                    int thresh = 16;
                    if (fgets(t_line, sizeof(t_line), stdin) && atoi(t_line) > 0) {
                        thresh = atoi(t_line);
                    }
                    char thresh_buf[16];
                    snprintf(thresh_buf, sizeof(thresh_buf), "%d", thresh);
                    char *args[] = { (char *)"set", (char *)m_str, thresh_buf, NULL };
                    cmd_smartshift(3, args);
                }
                break;
            }
            case 4:
                cmd_reload();
                break;
            case 5:
                cmd_monitor();
                break;
            default:
                printf(COLOR_YELLOW "Invalid option. Please select 0-5.\n" COLOR_RESET);
                break;
        }
    }

    ipc_client_close(&client);
    return 0;
}

/* --- Subcommand: autostart --- */
static int cmd_autostart(int argc, char **argv)
{
    const char *action = (argc > 0) ? argv[0] : "status";
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "[-] Could not determine HOME directory\n");
        return 1;
    }

#ifdef __APPLE__
    char plist_path[512];
    snprintf(plist_path, sizeof(plist_path), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);

    if (strcmp(action, "status") == 0) {
        bool exists = (access(plist_path, F_OK) == 0);
        int loaded = system("launchctl list io.github.openlogi.agent > /dev/null 2>&1");
        bool is_loaded = (loaded == 0);

        if (g_json_output) {
            printf("{\"autostart\":%s,\"installed\":%s,\"service\":\"launchd\",\"path\":\"%s\"}\n",
                   is_loaded ? "true" : "false", exists ? "true" : "false", plist_path);
            return 0;
        }

        print_banner();
        printf("Autostart Service:   macOS launchd (User Agent)\n");
        printf("Configuration Plist: %s\n", plist_path);
        printf("Service Status:      %s (%s)\n\n",
               is_loaded ? COLOR_BOLD COLOR_GREEN "ENABLED & RUNNING" COLOR_RESET :
               (exists ? COLOR_YELLOW "INSTALLED (Not loaded)" COLOR_RESET : COLOR_RED "DISABLED (Not installed)" COLOR_RESET),
               is_loaded ? "loaded in launchd" : "not running under launchd");
        printf("Subcommands:\n");
        printf("  openlogictl autostart enable   - Install LaunchAgent and launch openlogid on login\n");
        printf("  openlogictl autostart disable  - Unload and remove LaunchAgent\n");
        return 0;
    } else if (strcmp(action, "enable") == 0) {
        char agents_dir[512];
        snprintf(agents_dir, sizeof(agents_dir), "%s/Library/LaunchAgents", home);
        mkdir(agents_dir, 0755);

        char bin_path[512] = "/usr/local/bin/openlogid";
        if (access(bin_path, X_OK) != 0) {
            if (access("bin/openlogid", X_OK) == 0) {
                realpath("bin/openlogid", bin_path);
            }
        }

        FILE *f = fopen(plist_path, "w");
        if (!f) {
            fprintf(stderr, "[-] Failed to write plist to %s\n", plist_path);
            return 1;
        }

        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
        fprintf(f, "<plist version=\"1.0\">\n<dict>\n");
        fprintf(f, "    <key>Label</key>\n    <string>io.github.openlogi.agent</string>\n");
        fprintf(f, "    <key>ProgramArguments</key>\n    <array>\n");
        fprintf(f, "        <string>%s</string>\n", bin_path);
        fprintf(f, "    </array>\n");
        fprintf(f, "    <key>RunAtLoad</key>\n    <true/>\n");
        fprintf(f, "    <key>KeepAlive</key>\n    <true/>\n");
        fprintf(f, "    <key>ProcessType</key>\n    <string>Interactive</string>\n");
        fprintf(f, "    <key>StandardOutPath</key>\n    <string>/tmp/openlogid.stdout.log</string>\n");
        fprintf(f, "    <key>StandardErrorPath</key>\n    <string>/tmp/openlogid.stderr.log</string>\n");
        fprintf(f, "</dict>\n</plist>\n");
        fclose(f);

        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "launchctl unload \"%s\" > /dev/null 2>&1; launchctl load -w \"%s\"", plist_path, plist_path);
        (void)system(cmd);

        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"enabled\",\"path\":\"%s\",\"binary\":\"%s\"}\n", plist_path, bin_path);
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] Autostart successfully enabled!" COLOR_RESET "\n");
            printf("    Registered LaunchAgent: %s\n", plist_path);
            printf("    Daemon binary:          %s\n", bin_path);
            printf("    openlogid will now automatically launch on user login.\n");
        }
        return 0;
    } else if (strcmp(action, "disable") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "launchctl unload -w \"%s\" > /dev/null 2>&1", plist_path);
        (void)system(cmd);
        unlink(plist_path);

        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"disabled\"}\n");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] Autostart disabled." COLOR_RESET "\n");
            printf("    Removed LaunchAgent: %s\n", plist_path);
        }
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown action '%s'. Use 'status', 'enable', or 'disable'.\n", action);
        return 1;
    }
#else
    char service_path[512];
    snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/openlogid.service", home);

    if (strcmp(action, "status") == 0) {
        bool exists = (access(service_path, F_OK) == 0);
        int ret = system("systemctl --user is-active --quiet openlogid.service 2>/dev/null");
        bool is_active = (ret == 0);

        if (g_json_output) {
            printf("{\"autostart\":%s,\"installed\":%s,\"service\":\"systemd\",\"path\":\"%s\"}\n",
                   is_active ? "true" : "false", exists ? "true" : "false", service_path);
            return 0;
        }

        print_banner();
        printf("Autostart Service:   Linux systemd (User Unit)\n");
        printf("Service File:        %s\n", service_path);
        printf("Service Status:      %s\n\n",
               is_active ? COLOR_BOLD COLOR_GREEN "ACTIVE & RUNNING" COLOR_RESET :
               (exists ? COLOR_YELLOW "INSTALLED (Inactive)" COLOR_RESET : COLOR_RED "DISABLED (Not installed)" COLOR_RESET));
        return 0;
    } else if (strcmp(action, "enable") == 0) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/.config/systemd/user", home);
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s && cp packaging/linux/openlogid.service %s && systemctl --user daemon-reload && systemctl --user enable --now openlogid.service", dir, service_path);
        (void)system(cmd);
        printf(COLOR_BOLD COLOR_GREEN "[+] Autostart successfully enabled with systemd user unit!" COLOR_RESET "\n");
        return 0;
    } else if (strcmp(action, "disable") == 0) {
        (void)system("systemctl --user disable --now openlogid.service 2>/dev/null");
        unlink(service_path);
        printf(COLOR_BOLD COLOR_GREEN "[+] Autostart disabled." COLOR_RESET "\n");
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown action '%s'. Use 'status', 'enable', or 'disable'.\n", action);
        return 1;
    }
#endif
}

static void print_usage(const char *progname)
{
    printf("OpenLogi Pure C Control Tool (openlogictl) v%s\n", OPENLOGI_AGENT_VERSION);
    printf("Usage: %s [options] <command> [arguments]\n\n", progname);
    printf("Commands:\n");
    printf("  status                         Show daemon health, permissions, and active app\n");
    printf("  list, inventory                List connected devices and properties\n");
    printf("  dpi get                        Get current sensor DPI\n");
    printf("  dpi set <value> [dev]          Set active sensor DPI in real time\n");
    printf("  smartshift get                 Get SmartShift mode and threshold\n");
    printf("  smartshift set <mode> <thresh> Configure SmartShift wheel (ratchet/freespin/auto)\n");
    printf("  reload                         Hot-reload config.toml immediately\n");
    printf("  monitor                        Stream real-time intercepted events and gestures\n");
    printf("  setup                          Launch interactive ANSI terminal setup wizard\n");
    printf("  autostart [status|enable|disable] Configure automatic login autostart\n\n");
    printf("Options:\n");
    printf("  --json                         Output in machine-readable JSON format\n");
    printf("  -s, --socket <path>            Specify custom UNIX domain socket path\n");
    printf("  -a, --autospawn                Automatically start daemon if not running\n");
    printf("  -h, --help                     Show this help message\n");
}

int main(int argc, char **argv)
{
    static struct option long_options[] = {
        {"json",      no_argument,       0, 'j'},
        {"socket",    required_argument, 0, 's'},
        {"autospawn", no_argument,       0, 'a'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "+jas:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j': g_json_output = true; break;
            case 'a': g_autospawn = true; break;
            case 's': strncpy(g_custom_socket, optarg, sizeof(g_custom_socket) - 1); break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        /* Default action when no subcommand is provided: show status */
        return cmd_status();
    }

    const char *cmd = argv[optind];
    int sub_argc = argc - optind - 1;
    char **sub_argv = &argv[optind + 1];

    if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    } else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "inventory") == 0) {
        return cmd_list();
    } else if (strcmp(cmd, "dpi") == 0) {
        return cmd_dpi(sub_argc, sub_argv);
    } else if (strcmp(cmd, "smartshift") == 0) {
        return cmd_smartshift(sub_argc, sub_argv);
    } else if (strcmp(cmd, "reload") == 0) {
        return cmd_reload();
    } else if (strcmp(cmd, "monitor") == 0) {
        return cmd_monitor();
    } else if (strcmp(cmd, "setup") == 0) {
        return cmd_setup();
    } else if (strcmp(cmd, "autostart") == 0) {
        return cmd_autostart(sub_argc, sub_argv);
    } else {
        fprintf(stderr, "[-] Unknown command '%s'. Run '%s --help' for available commands.\n", cmd, argv[0]);
        return 1;
    }
}
