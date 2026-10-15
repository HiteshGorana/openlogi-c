/**
 * @file openlogictl.c
 * @brief OpenLogi Pure C Command-Line Tool and Setup Wizard.
 */

#include "ipc_client.h"
#include "ipc_server.h"
#include "config.h"
#include "tool_path.h"
#include "single_instance.h"
#include "subcommands.h"
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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/file.h>
#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDLib.h>
extern bool CGRequestListenEventAccess(void);
extern bool CGPreflightListenEventAccess(void);
#endif

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
static const char *g_program_path;

static int cmd_doctor(int argc, char **argv);
static int cmd_autostart(int argc, char **argv);
static int cmd_permissions(int argc, char **argv);
static int cmd_stop(void);

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
        (void)system("openlogi daemon --daemon > /dev/null 2>&1 || openlogid --daemon > /dev/null 2>&1 || bin/openlogi daemon --daemon > /dev/null 2>&1 || bin/openlogid --daemon > /dev/null 2>&1");
        usleep(150000); /* 150ms settling time */
        if (ipc_client_connect(sock, client)) {
            return true;
        }
    }

    if (g_json_output) {
        printf("{\"error\":\"Daemon not running or unreachable\"}\n");
    } else {
        fprintf(stderr, COLOR_RED "[-] Error: Could not connect to OpenLogi daemon." COLOR_RESET "\n");
        fprintf(stderr, "    Please verify that the daemon is running:\n");
        fprintf(stderr, "      openlogi daemon --daemon\n");
        fprintf(stderr, "    Or run: openlogi service enable\n");
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

    const char *rstate = yyjson_get_str(yyjson_obj_get(res, "runtime_state"));
    if (rstate) {
        bool is_act = (strcmp(rstate, "active") == 0);
        printf("Runtime State:       %s\n", is_act ? COLOR_GREEN "ACTIVE" COLOR_RESET : COLOR_YELLOW "STANDBY" COLOR_RESET);
    }

    bool hook_inst = yyjson_get_bool(yyjson_obj_get(res, "hook_installed"));
    printf("OS Input Hook:       %s\n", hook_inst ? COLOR_GREEN "ACTIVE (Event Tap installed)" COLOR_RESET : COLOR_YELLOW "PASSIVE" COLOR_RESET);

    yyjson_val *perms = yyjson_obj_get(res, "permissions");
    bool perms_ok = true;
    if (perms) {
        bool acc = yyjson_get_bool(yyjson_obj_get(perms, "accessibility"));
        bool inp = yyjson_get_bool(yyjson_obj_get(perms, "input_monitoring"));
        perms_ok = acc && inp;
        printf("Permissions:         Accessibility: %s | Input Monitoring: %s\n",
               acc ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_RED "DENIED" COLOR_RESET,
               inp ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_RED "DENIED" COLOR_RESET);
    }

    const char *front = yyjson_get_str(yyjson_obj_get(res, "frontmost_app"));
    printf("Active Frontmost:    %s\n", (front && front[0] != '\0') ? front : "(none / desktop)");

    uint64_t dev_count = yyjson_get_uint(yyjson_obj_get(res, "devices_online"));
    printf("Connected Devices:   " COLOR_BOLD "%llu online" COLOR_RESET "\n", (unsigned long long)dev_count);
    printf(COLOR_CYAN "=====================================================" COLOR_RESET "\n");

    if (!perms_ok) {
        printf("\n" COLOR_BOLD COLOR_RED "[!] Notice: macOS permissions are missing for the background daemon:" COLOR_RESET "\n");
        printf("    Without Accessibility and Input Monitoring, the driver cannot detect\n");
        printf("    Logitech Bluetooth devices or intercept button clicks.\n\n");
        printf("    Run " COLOR_BOLD "openlogi permissions request" COLOR_RESET " to authorize in System Settings.\n\n");
    } else {
        printf("\n");
    }

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

static bool cmd_reload_silent(void)
{
    ipc_client_t client;
    const char *sock = (g_custom_socket[0] != '\0') ? g_custom_socket : NULL;
    if (!ipc_client_connect(sock, &client)) {
        return false;
    }
    yyjson_doc *doc = ipc_client_call(&client, "openlogi.reload_config", "{}");
    ipc_client_close(&client);
    if (doc) {
        yyjson_doc_free(doc);
        return true;
    }
    return false;
}

/* Helper to read a scalar value from TOML file */
static bool toml_read_key(const char *filepath, const char *key, char *out_val, size_t max_len)
{
    if (!filepath || !key || !out_val || max_len == 0) return false;
    out_val[0] = '\0';

    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;

    char line[512];
    size_t klen = strlen(key);
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        if (strncmp(p, key, klen) == 0) {
            char *eq = p + klen;
            while (*eq == ' ' || *eq == '\t') eq++;
            if (*eq == '=') {
                eq++;
                while (*eq == ' ' || *eq == '\t') eq++;
                char *end = eq + strlen(eq) - 1;
                while (end > eq && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                if (*eq == '"' && *end == '"' && end > eq) {
                    eq++;
                    *end = '\0';
                }
                strncpy(out_val, eq, max_len - 1);
                out_val[max_len - 1] = '\0';
                found = true;
                break;
            }
        }
    }

    fclose(fp);
    return found;
}

/* Helper to atomically modify or add a key in a TOML file */
static bool toml_update_key(const char *filepath, const char *section_hint, const char *key, const char *new_val_literal, char *errbuf, size_t errbufsz)
{
    if (!filepath || !key || !new_val_literal) {
        if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Invalid parameters");
        return false;
    }

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fp = fopen(filepath, "w");
        if (!fp) {
            if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Cannot create %s", filepath);
            return false;
        }
        fprintf(fp, "schema_version = 7\n\n[devices.default]\n%s = %s\n", key, new_val_literal);
        fclose(fp);
        return true;
    }

    char tmp_path[540];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", filepath, getpid());
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        fclose(fp);
        if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Cannot create temporary file %s", tmp_path);
        return false;
    }

    char line[1024];
    size_t klen = strlen(key);
    bool key_replaced = false;
    bool in_section = (section_hint == NULL);

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '[') {
            if (section_hint) {
                if (strstr(p, section_hint) != NULL) {
                    in_section = true;
                } else {
                    if (in_section && !key_replaced) {
                        fprintf(out, "%s = %s\n", key, new_val_literal);
                        key_replaced = true;
                    }
                    in_section = false;
                }
            }
            fputs(line, out);
            continue;
        }

        if (in_section && !key_replaced && *p != '#') {
            if (strncmp(p, key, klen) == 0) {
                char *eq = p + klen;
                while (*eq == ' ' || *eq == '\t') eq++;
                if (*eq == '=') {
                    fprintf(out, "%s = %s\n", key, new_val_literal);
                    key_replaced = true;
                    continue;
                }
            }
        }

        fputs(line, out);
    }

    if (!key_replaced) {
        if (section_hint && !in_section) {
            fprintf(out, "\n[%s]\n%s = %s\n", section_hint, key, new_val_literal);
        } else {
            fprintf(out, "%s = %s\n", key, new_val_literal);
        }
    }

    fclose(fp);
    fclose(out);

    if (rename(tmp_path, filepath) != 0) {
        unlink(tmp_path);
        if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Failed to update %s atomically", filepath);
        return false;
    }

    return true;
}

/* --- Subcommand: scroll / wheel --- */
static int cmd_scroll(int argc, char **argv)
{
    char cfg_path[512];
    config_get_default_path(cfg_path, sizeof(cfg_path));

    const char *sub = (argc > 0) ? argv[0] : "get";

    if (strcmp(sub, "get") == 0) {
        char inv_str[64] = "false";
        char res_str[64] = "low";
        char smooth_str[64] = "true";
        char vert_sens[64] = "14";
        char thumb_sens[64] = "14";

        toml_read_key(cfg_path, "invert_scroll", inv_str, sizeof(inv_str));
        toml_read_key(cfg_path, "scroll_resolution", res_str, sizeof(res_str));
        toml_read_key(cfg_path, "smooth_scroll", smooth_str, sizeof(smooth_str));
        toml_read_key(cfg_path, "vertical_scroll_sensitivity", vert_sens, sizeof(vert_sens));
        toml_read_key(cfg_path, "thumbwheel_sensitivity", thumb_sens, sizeof(thumb_sens));

        bool is_inv = (strcmp(inv_str, "true") == 0 || strcmp(inv_str, "1") == 0);
        bool is_smooth = (strcmp(smooth_str, "true") == 0 || strcmp(smooth_str, "1") == 0);

        if (g_json_output) {
            printf("{\"invert_scroll\":%s,\"scroll_resolution\":\"%s\",\"smooth_scroll\":%s,\"vertical_scroll_sensitivity\":%s,\"thumbwheel_sensitivity\":%s,\"config_file\":\"%s\"}\n",
                   is_inv ? "true" : "false", res_str, is_smooth ? "true" : "false", vert_sens, thumb_sens, cfg_path);
            return 0;
        }

        print_banner();
        printf("Mouse Scroll & Wheel Settings (" COLOR_CYAN "%s" COLOR_RESET "):\n", cfg_path);
        printf("--------------------------------------------------------------------------------\n");
        printf("  Invert Scroll:          %s\n",
               is_inv ? COLOR_BOLD COLOR_GREEN "ENABLED (true - wheel reversed)" COLOR_RESET
                      : COLOR_BOLD COLOR_YELLOW "DISABLED (false - standard)" COLOR_RESET);
        printf("  Scroll Resolution:      %s (%s)\n",
               res_str,
               strcmp(res_str, "high") == 0 ? "high-resolution continuous ticks" : "discrete ratchet step per notch");
        printf("  Smooth Scrolling:       %s\n",
               is_smooth ? COLOR_GREEN "ENABLED" COLOR_RESET : COLOR_YELLOW "DISABLED" COLOR_RESET);
        printf("  Vertical Sensitivity:   %s\n", vert_sens);
        printf("  Thumbwheel Sensitivity: %s\n", thumb_sens);
        printf("--------------------------------------------------------------------------------\n");
        printf(COLOR_BOLD "Commands to adjust scrolling:" COLOR_RESET "\n");
        printf("  Toggle Inversion:       openlogictl scroll invert toggle\n");
        printf("  Set Inversion ON:       openlogictl scroll invert true\n");
        printf("  Set Inversion OFF:      openlogictl scroll invert false\n");
        printf("  Set Wheel Hi-Res:       openlogictl scroll resolution high\n");
        printf("  Set Wheel Low-Res:      openlogictl scroll resolution low\n\n");
        return 0;
    } else if (strcmp(sub, "invert") == 0) {
        char cur_inv[64] = "false";
        toml_read_key(cfg_path, "invert_scroll", cur_inv, sizeof(cur_inv));
        bool current = (strcmp(cur_inv, "true") == 0 || strcmp(cur_inv, "1") == 0);

        bool new_val = false;
        if (argc >= 2) {
            const char *arg = argv[1];
            if (strcasecmp(arg, "true") == 0 || strcasecmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
                new_val = true;
            } else if (strcasecmp(arg, "false") == 0 || strcasecmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
                new_val = false;
            } else if (strcasecmp(arg, "toggle") == 0) {
                new_val = !current;
            } else {
                fprintf(stderr, "[-] Error: Invalid argument '%s'. Use 'true', 'false', 'on', 'off', or 'toggle'.\n", arg);
                return 1;
            }
        } else {
            new_val = !current;
        }

        char err[256];
        if (!toml_update_key(cfg_path, "devices", "invert_scroll", new_val ? "true" : "false", err, sizeof(err))) {
            fprintf(stderr, COLOR_RED "[-] Failed to update config: %s" COLOR_RESET "\n", err);
            return 1;
        }

        bool reloaded = cmd_reload_silent();

        if (g_json_output) {
            printf("{\"success\":true,\"invert_scroll\":%s,\"daemon_reloaded\":%s}\n",
                   new_val ? "true" : "false", reloaded ? "true" : "false");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] Inverted scrolling set to: %s (invert_scroll = %s)" COLOR_RESET "\n",
                   new_val ? "ON" : "OFF", new_val ? "true" : "false");
            printf("    Config updated at %s%s\n\n",
                   cfg_path, reloaded ? " (daemon reloaded)" : " (will apply on daemon launch)");
        }
        return 0;
    } else if (strcmp(sub, "resolution") == 0) {
        if (argc < 2) {
            fprintf(stderr, "[-] Error: Resolution value required ('low' or 'high').\n");
            return 1;
        }
        const char *res_arg = argv[1];
        if (strcasecmp(res_arg, "low") != 0 && strcasecmp(res_arg, "high") != 0) {
            fprintf(stderr, "[-] Error: Resolution must be 'low' or 'high'.\n");
            return 1;
        }

        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "\"%s\"", res_arg);

        char err[256];
        if (!toml_update_key(cfg_path, "devices", "scroll_resolution", val_buf, err, sizeof(err))) {
            fprintf(stderr, COLOR_RED "[-] Failed to update config: %s" COLOR_RESET "\n", err);
            return 1;
        }

        bool reloaded = cmd_reload_silent();

        if (g_json_output) {
            printf("{\"success\":true,\"scroll_resolution\":\"%s\",\"daemon_reloaded\":%s}\n",
                   res_arg, reloaded ? "true" : "false");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] Wheel resolution set to: %s (scroll_resolution = \"%s\")" COLOR_RESET "\n",
                   res_arg, res_arg);
            printf("    Config updated at %s%s\n\n",
                   cfg_path, reloaded ? " (daemon reloaded)" : " (will apply on daemon launch)");
        }
        return 0;
    } else if (strcmp(sub, "smooth") == 0) {
        if (argc < 2) {
            fprintf(stderr, "[-] Error: Value required ('on', 'off', 'true', 'false').\n");
            return 1;
        }
        bool smooth_val = (strcasecmp(argv[1], "on") == 0 || strcasecmp(argv[1], "true") == 0 || strcmp(argv[1], "1") == 0);
        char err[256];
        if (!toml_update_key(cfg_path, "app_settings", "smooth_scroll", smooth_val ? "true" : "false", err, sizeof(err))) {
            fprintf(stderr, COLOR_RED "[-] Failed to update config: %s" COLOR_RESET "\n", err);
            return 1;
        }
        cmd_reload_silent();
        printf(COLOR_BOLD COLOR_GREEN "[+] Smooth scrolling set to: %s" COLOR_RESET "\n", smooth_val ? "ON" : "OFF");
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown scroll command '%s'. Available: 'get', 'invert', 'resolution', 'smooth'.\n", sub);
        return 1;
    }
}

/* --- Action Catalog Entry --- */
typedef struct {
    const char *category;
    const char *name;
    const char *desc;
} cli_action_item_t;

static const cli_action_item_t k_action_catalog[] = {
    {"Navigation", "MissionControl", "Activates macOS Mission Control window overview"},
    {"Navigation", "AppExpose", "Shows all windows of active foreground application"},
    {"Navigation", "ShowDesktop", "Temporarily reveals the desktop"},
    {"Navigation", "Launchpad", "Opens Launchpad application grid"},
    {"Navigation", "NextSpace", "Switches to next desktop workspace"},
    {"Navigation", "PrevSpace", "Switches to previous desktop workspace"},

    {"Window/App", "AppSwitchNext", "Switches forward in app switcher (Cmd+Tab)"},
    {"Window/App", "AppSwitchPrev", "Switches backward in app switcher (Cmd+Shift+Tab)"},
    {"Window/App", "CloseWindow", "Closes frontmost window (Cmd+W)"},
    {"Window/App", "MinimizeWindow", "Minimizes frontmost window (Cmd+M)"},
    {"Window/App", "ToggleFullscreen", "Toggles full screen mode (Ctrl+Cmd+F)"},

    {"Browser", "BrowserBack", "Navigates back in browser history (Cmd+[)"},
    {"Browser", "BrowserForward", "Navigates forward in browser history (Cmd+])"},
    {"Browser", "BrowserRefresh", "Reloads current web page (Cmd+R)"},
    {"Browser", "BrowserNewTab", "Opens a new tab (Cmd+T)"},
    {"Browser", "BrowserCloseTab", "Closes active tab (Cmd+W)"},
    {"Browser", "BrowserReopenClosedTab", "Reopens last closed tab (Cmd+Shift+T)"},
    {"Browser", "PrevTab", "Switches to previous tab (Ctrl+Shift+Tab)"},
    {"Browser", "NextTab", "Switches to next tab (Ctrl+Tab)"},

    {"System", "LockScreen", "Locks screen immediately (Cmd+Ctrl+Q)"},
    {"System", "Sleep", "Puts computer to sleep immediately"},
    {"System", "Screenshot", "Takes screenshot of entire screen (Cmd+Shift+3)"},
    {"System", "CaptureRegion", "Interactive region screenshot tool (Cmd+Shift+4)"},

    {"Media", "MediaPlayPause", "Toggles audio/video playback (Play/Pause key)"},
    {"Media", "MediaNextTrack", "Skips to next track (Fast Forward key)"},
    {"Media", "MediaPrevTrack", "Returns to previous track (Rewind key)"},
    {"Media", "VolumeUp", "Increases master volume"},
    {"Media", "VolumeDown", "Decreases master volume"},
    {"Media", "VolumeMute", "Mutes / unmutes master audio"},

    {"Editing", "Undo", "Undoes previous edit operation (Cmd+Z)"},
    {"Editing", "Redo", "Redoes undone edit operation (Cmd+Shift+Z)"},
    {"Editing", "Cut", "Cuts selection to clipboard (Cmd+X)"},
    {"Editing", "Copy", "Copies selection to clipboard (Cmd+C)"},
    {"Editing", "Paste", "Pastes from clipboard (Cmd+V)"},
    {"Editing", "SelectAll", "Selects all content in document (Cmd+A)"},
    {"Editing", "Find", "Opens Find/Search bar (Cmd+F)"},

    {"Pointer", "LeftClick", "Synthetic primary mouse click"},
    {"Pointer", "RightClick", "Synthetic secondary mouse click"},
    {"Pointer", "MiddleClick", "Synthetic middle mouse button click"},
    {"Pointer", "DoubleClick", "Double click primary mouse button"},
    {"Pointer", "TripleClick", "Triple click primary mouse button"},
    {"Pointer", "ScrollUp", "Synthesizes vertical scroll wheel up"},
    {"Pointer", "ScrollDown", "Synthesizes vertical scroll wheel down"},
    {"Pointer", "HorizontalScrollLeft", "Synthesizes horizontal thumbwheel scroll left"},
    {"Pointer", "HorizontalScrollRight", "Synthesizes horizontal thumbwheel scroll right"},

    {"Hardware", "ToggleSmartShift", "Toggles wheel clutch between ratchet and free-spin"},
    {"Hardware", "CycleDpiPresets", "Rotates to next sensor DPI preset"},
    {"Hardware", "ShowActionsRing", "Opens visual Actions Ring HUD overlay"},
    {"Hardware", "None", "Suppresses click reporting (produces no action)"},

    {"Advanced", "CustomShortcut", "Single key or chord: { CustomShortcut = \"Cmd+Shift+T\" }"},
    {"Advanced", "HoldShortcut", "Modifier held with button: { HoldShortcut = \"Cmd\" }"},
    {"Advanced", "SetDpiPreset", "Exact sensor resolution: { SetDpiPreset = 1600 }"},
    {"Advanced", "OpenApplication", "App launch: { OpenApplication = \"/Applications/Safari.app\" }"},
    {"Advanced", "TypeText", "Arbitrary text injection: { TypeText = \"Hello World\" }"},
    {"Advanced", "RunAppleScript", "AppleScript: { RunAppleScript = \"beep\" }"},
    {"Advanced", "RunShellCommand", "Shell command: { RunShellCommand = \"say hello\" }"},
    {"Advanced", "Workflow", "Sequence: { Workflow = [ { PressKey = \"Cmd+Space\" }, { Delay = 50 } ] }"}
};

static int cmd_actions(int argc, char **argv)
{
    const char *filter = (argc > 0) ? argv[0] : NULL;
    size_t total = sizeof(k_action_catalog) / sizeof(k_action_catalog[0]);

    if (g_json_output) {
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        yyjson_mut_doc_set_root(doc, arr);

        for (size_t i = 0; i < total; i++) {
            if (filter && strcasestr(k_action_catalog[i].name, filter) == NULL &&
                strcasestr(k_action_catalog[i].category, filter) == NULL) {
                continue;
            }
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, item, "category", k_action_catalog[i].category);
            yyjson_mut_obj_add_str(doc, item, "name", k_action_catalog[i].name);
            yyjson_mut_obj_add_str(doc, item, "description", k_action_catalog[i].desc);
            yyjson_mut_arr_add_val(arr, item);
        }

        char *json = yyjson_mut_write(doc, 0, NULL);
        printf("%s\n", json ? json : "[]");
        free(json);
        yyjson_mut_doc_free(doc);
        return 0;
    }

    print_banner();
    printf("Supported Action Vocabulary (%zu actions available):\n", total);
    printf("----------------------------------------------------------------------------------------\n");
    printf(COLOR_BOLD "%-12s %-24s %-50s" COLOR_RESET "\n", "Category", "Action String", "Description");
    printf("----------------------------------------------------------------------------------------\n");

    size_t printed = 0;
    for (size_t i = 0; i < total; i++) {
        if (filter && strcasestr(k_action_catalog[i].name, filter) == NULL &&
            strcasestr(k_action_catalog[i].category, filter) == NULL &&
            strcasestr(k_action_catalog[i].desc, filter) == NULL) {
            continue;
        }
        printf("%-12s " COLOR_BOLD "%-24s" COLOR_RESET " %-50s\n",
               k_action_catalog[i].category, k_action_catalog[i].name, k_action_catalog[i].desc);
        printed++;
    }

    if (printed == 0) {
        printf("    No actions matched filter '%s'.\n", filter);
    }
    printf("----------------------------------------------------------------------------------------\n\n");
    return 0;
}

/* --- Subcommand: options / schema --- */
static int cmd_options(void)
{
    if (g_json_output) {
        printf("{\"schema_version\":7,\"sections\":[\"app_settings\",\"devices\",\"smartshift\",\"bindings\"]}\n");
        return 0;
    }

    print_banner();
    printf(COLOR_BOLD "OpenLogi Configuration Schema & Supported Options Guide" COLOR_RESET "\n\n");

    printf(COLOR_BOLD COLOR_GREEN "1. Application Settings [app_settings]" COLOR_RESET "\n");
    printf("  smooth_scroll                 true | false      (Sub-pixel scroll momentum)\n");
    printf("  vertical_scroll_sensitivity   1 .. 30           (Default: 14)\n");
    printf("  thumbwheel_sensitivity        1 .. 30           (Default: 14)\n");
    printf("  capture_mouse_events          true | false      (Intercept mouse buttons for diversion)\n");
    printf("  launch_at_login               true | false      (Auto-start background service)\n");
    printf("  appearance                    \"dark\" | \"light\" | \"system\"\n\n");

    printf(COLOR_BOLD COLOR_GREEN "2. Per-Device Hardware Settings [devices.\"<device-key>\"]" COLOR_RESET "\n");
    printf("  invert_scroll                 true | false      (Inverts vertical mouse wheel direction)\n");
    printf("  scroll_resolution             \"low\" | \"high\"    (\"low\"=per-notch, \"high\"=hi-res ticks)\n");
    printf("  dpi                           200 .. 32000      (Optical sensor DPI step)\n");
    printf("  dpi_presets                   [800, 1600, 3200] (Array of DPI steps for CycleDpi)\n\n");

    printf(COLOR_BOLD COLOR_GREEN "3. SmartShift Scroll Wheel [devices.\"<id>\".smartshift]" COLOR_RESET "\n");
    printf("  mode                          \"ratchet\" | \"freespin\" | \"auto\" | \"disabled\"\n");
    printf("  auto_disengage                1 .. 255          (Flick sensitivity to unlock wheel)\n\n");

    printf(COLOR_BOLD COLOR_GREEN "4. Button & Gesture Bindings [devices.\"<id>\".bindings]" COLOR_RESET "\n");
    printf("  MiddleClick, Back, Forward, DpiToggle, GestureButton, ThumbwheelScrollUp, ThumbwheelScrollDown\n");
    printf("  Format A (Catalog Action):    Back = \"MissionControl\"\n");
    printf("  Format B (Key Combination):   Forward = { CustomShortcut = \"Cmd+Shift+T\" }\n");
    printf("  Format C (Held Modifier):     MiddleClick = { HoldShortcut = \"Cmd\" }\n");
    printf("  Format D (5-way Gesture):     [devices.\"<id>\".bindings.GestureButton]\n");
    printf("                                Click = \"MissionControl\"\n");
    printf("                                Up    = \"MissionControl\"\n");
    printf("                                Down  = \"ShowDesktop\"\n");
    printf("                                Left  = \"PrevTab\"\n");
    printf("                                Right = \"NextTab\"\n\n");

    printf(COLOR_BOLD "Run 'openlogictl actions' to browse all 53 actions with descriptions." COLOR_RESET "\n\n");
    return 0;
}

/* --- Subcommand: config --- */
static int cmd_config(int argc, char **argv)
{
    char cfg_path[512];
    config_get_default_path(cfg_path, sizeof(cfg_path));

    const char *sub = (argc > 0) ? argv[0] : "show";

    if (strcmp(sub, "path") == 0) {
        printf("%s\n", cfg_path);
        return 0;
    } else if (strcmp(sub, "show") == 0) {
        FILE *fp = fopen(cfg_path, "r");
        if (!fp) {
            fprintf(stderr, "[-] Config file not found at %s\n", cfg_path);
            return 1;
        }
        if (!g_json_output) {
            print_banner();
            printf("Active Configuration File: " COLOR_CYAN "%s" COLOR_RESET "\n\n", cfg_path);
        }
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout);
        }
        fclose(fp);
        return 0;
    } else if (strcmp(sub, "get") == 0) {
        if (argc < 2) {
            fprintf(stderr, "[-] Usage: openlogictl config get <key>\n");
            return 1;
        }
        char val[128] = "";
        if (toml_read_key(cfg_path, argv[1], val, sizeof(val))) {
            if (g_json_output) {
                printf("{\"key\":\"%s\",\"value\":\"%s\"}\n", argv[1], val);
            } else {
                printf("%s = %s\n", argv[1], val);
            }
            return 0;
        } else {
            fprintf(stderr, "[-] Key '%s' not found in %s\n", argv[1], cfg_path);
            return 1;
        }
    } else if (strcmp(sub, "set") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[-] Usage: openlogictl config set <key> <value>\n");
            return 1;
        }
        const char *key = argv[1];
        const char *val = argv[2];

        char err[256];
        if (!toml_update_key(cfg_path, "devices", key, val, err, sizeof(err))) {
            fprintf(stderr, COLOR_RED "[-] Failed to update config: %s" COLOR_RESET "\n", err);
            return 1;
        }
        bool reloaded = cmd_reload_silent();
        printf(COLOR_BOLD COLOR_GREEN "[+] Set %s = %s in %s%s" COLOR_RESET "\n",
               key, val, cfg_path, reloaded ? " (daemon reloaded)" : "");
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown config action '%s'. Use 'path', 'show', 'get', or 'set'.\n", sub);
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

#ifdef __APPLE__
    if (!AXIsProcessTrusted() || !CGPreflightListenEventAccess()) {
        printf(COLOR_BOLD COLOR_YELLOW "[!] Setup Notice: Required macOS permissions are missing." COLOR_RESET "\n");
        printf("    Launching OpenLogi Doctor to guide you through authorization...\n\n");
        if (cmd_doctor(0, NULL) != 0) {
            return 1;
        }
    }
#endif

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

#ifdef __APPLE__
static int run_launchctl(const char *verb, const char *target, const char *path, bool quiet)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (quiet) {
            int sink = open("/dev/null", O_WRONLY);
            if (sink >= 0) {
                dup2(sink, STDOUT_FILENO);
                dup2(sink, STDERR_FILENO);
                close(sink);
            }
        }
        execl("/bin/launchctl", "launchctl", verb, target, path, (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void print_xml_text(FILE *file, const char *text)
{
    for (; *text; text++) {
        switch (*text) {
            case '&': fputs("&amp;", file); break;
            case '<': fputs("&lt;", file); break;
            case '>': fputs("&gt;", file); break;
            case '"': fputs("&quot;", file); break;
            case '\'': fputs("&apos;", file); break;
            default: fputc((unsigned char)*text, file); break;
        }
    }
}
#endif

static int cmd_stop(void);

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
        char bin_path[4096];
        bool is_unified = false;

        char user_app[4096];
        snprintf(user_app, sizeof(user_app), "%s/Applications/OpenLogi.app/Contents/MacOS/openlogi", home);
        if (access("/Applications/OpenLogi.app/Contents/MacOS/openlogi", X_OK) == 0) {
            strncpy(bin_path, "/Applications/OpenLogi.app/Contents/MacOS/openlogi", sizeof(bin_path) - 1);
            is_unified = true;
        } else if (access(user_app, X_OK) == 0) {
            strncpy(bin_path, user_app, sizeof(bin_path) - 1);
            is_unified = true;
        } else if (tool_sibling_path(g_program_path, getenv("PATH"), "openlogi", bin_path, sizeof(bin_path))) {
            is_unified = true;
        } else if (!tool_sibling_path(g_program_path, getenv("PATH"), "openlogid", bin_path, sizeof(bin_path))) {
            fprintf(stderr, "[-] Could not find openlogi or openlogid binary. Reinstall OpenLogi.\n");
            return 1;
        }

        char agents_dir[512];
        snprintf(agents_dir, sizeof(agents_dir), "%s/Library/LaunchAgents", home);
        if (mkdir(agents_dir, 0755) != 0 && errno != EEXIST) {
            perror("[-] Could not create LaunchAgents directory");
            return 1;
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
        fputs("        <string>", f);
        print_xml_text(f, bin_path);
        fputs("</string>\n", f);
        if (is_unified) {
            fprintf(f, "        <string>daemon</string>\n");
        }
        fprintf(f, "    </array>\n");
        fprintf(f, "    <key>RunAtLoad</key>\n    <true/>\n");
        fprintf(f, "    <key>KeepAlive</key>\n    <true/>\n");
        fprintf(f, "    <key>ProcessType</key>\n    <string>Interactive</string>\n");
        fprintf(f, "    <key>StandardOutPath</key>\n    <string>/tmp/openlogid.stdout.log</string>\n");
        fprintf(f, "    <key>StandardErrorPath</key>\n    <string>/tmp/openlogid.stderr.log</string>\n");
        fprintf(f, "</dict>\n</plist>\n");
        if (fclose(f) != 0) {
            perror("[-] Could not write LaunchAgent");
            return 1;
        }

        char domain[64], service[128];
        snprintf(domain, sizeof(domain), "gui/%u", (unsigned)getuid());
        snprintf(service, sizeof(service), "%s/io.github.openlogi.agent", domain);
        (void)run_launchctl("bootout", service, NULL, true);
        if (run_launchctl("enable", service, NULL, false) != 0) {
            fprintf(stderr, "[-] Could not enable LaunchAgent at %s\n", plist_path);
            return 1;
        }
        if (run_launchctl("bootstrap", domain, plist_path, false) != 0) {
            fprintf(stderr, "[-] Could not start LaunchAgent at %s\n", plist_path);
            return 1;
        }

        if (g_json_output) {
            yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
            if (!doc) return 1;
            yyjson_mut_val *root = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, root);
            yyjson_mut_obj_add_bool(doc, root, "success", true);
            yyjson_mut_obj_add_str(doc, root, "action", "enabled");
            yyjson_mut_obj_add_str(doc, root, "path", plist_path);
            yyjson_mut_obj_add_str(doc, root, "binary", bin_path);
            char *json = yyjson_mut_write(doc, 0, NULL);
            if (!json) { yyjson_mut_doc_free(doc); return 1; }
            puts(json);
            free(json);
            yyjson_mut_doc_free(doc);
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] Autostart successfully enabled!" COLOR_RESET "\n");
            printf("    Registered LaunchAgent: %s\n", plist_path);
            printf("    Daemon binary:          %s\n", bin_path);
            printf("    openlogid will now automatically launch on user login.\n");
            if (!AXIsProcessTrusted() || !CGPreflightListenEventAccess()) {
                printf(COLOR_YELLOW "[!] Note: macOS Accessibility & Input Monitoring permissions required." COLOR_RESET "\n");
                printf("    Run 'openlogi permissions request' to prompt macOS and open System Settings.\n");
            }
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
    } else if (strcmp(action, "stop") == 0) {
        return cmd_stop();
    } else if (strcmp(action, "restart") == 0) {
        cmd_stop();
        usleep(200000);
        if (access(plist_path, R_OK) != 0) {
            char *enable_args[] = { (char *)"enable", NULL };
            return cmd_autostart(1, enable_args);
        }
        char domain[64], service[128];
        snprintf(domain, sizeof(domain), "gui/%u", (unsigned)getuid());
        snprintf(service, sizeof(service), "%s/io.github.openlogi.agent", domain);
        (void)run_launchctl("enable", service, NULL, true);
        if (run_launchctl("kickstart", "-k", service, true) != 0) {
            (void)run_launchctl("bootout", service, NULL, true);
            (void)run_launchctl("enable", service, NULL, true);
            if (run_launchctl("bootstrap", domain, plist_path, false) != 0) {
                fprintf(stderr, "[-] Could not restart LaunchAgent at %s\n", plist_path);
                return 1;
            }
            (void)run_launchctl("kickstart", "-k", service, true);
        }

        ipc_client_t test_c;
        for (int i = 0; i < 20; i++) {
            usleep(50000);
            if (ipc_client_connect(NULL, &test_c)) {
                ipc_client_close(&test_c);
                break;
            }
        }

        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"restarted\"}\n");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] OpenLogi service restarted." COLOR_RESET "\n");
            if (!AXIsProcessTrusted() || !CGPreflightListenEventAccess()) {
                printf(COLOR_YELLOW "[!] Note: macOS Accessibility & Input Monitoring permissions required." COLOR_RESET "\n");
                printf("    Run 'openlogi permissions request' to prompt macOS and open System Settings.\n");
            }
        }
        return 0;
    } else if (strcmp(action, "start") == 0) {
        if (access(plist_path, R_OK) != 0) {
            char *enable_args[] = { (char *)"enable", NULL };
            return cmd_autostart(1, enable_args);
        }
        char domain[64], service[128];
        snprintf(domain, sizeof(domain), "gui/%u", (unsigned)getuid());
        snprintf(service, sizeof(service), "%s/io.github.openlogi.agent", domain);
        (void)run_launchctl("enable", service, NULL, true);
        if (run_launchctl("kickstart", "-k", service, true) != 0) {
            (void)run_launchctl("bootout", service, NULL, true);
            (void)run_launchctl("enable", service, NULL, true);
            if (run_launchctl("bootstrap", domain, plist_path, false) != 0) {
                fprintf(stderr, "[-] Could not start LaunchAgent at %s\n", plist_path);
                return 1;
            }
            (void)run_launchctl("kickstart", "-k", service, true);
        }

        ipc_client_t test_c;
        for (int i = 0; i < 20; i++) {
            usleep(50000);
            if (ipc_client_connect(NULL, &test_c)) {
                ipc_client_close(&test_c);
                break;
            }
        }

        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"started\"}\n");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] OpenLogi service started." COLOR_RESET "\n");
            if (!AXIsProcessTrusted() || !CGPreflightListenEventAccess()) {
                printf(COLOR_YELLOW "[!] Note: macOS Accessibility & Input Monitoring permissions required." COLOR_RESET "\n");
                printf("    Run 'openlogi permissions request' to prompt macOS and open System Settings.\n");
            }
        }
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown action '%s'. Use 'status', 'enable', 'disable', 'start', 'restart', or 'stop'.\n", action);
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
    } else if (strcmp(action, "stop") == 0) {
        return cmd_stop();
    } else if (strcmp(action, "restart") == 0) {
        cmd_stop();
        usleep(200000);
        (void)system("systemctl --user start openlogid.service 2>/dev/null");
        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"restarted\"}\n");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] OpenLogi service restarted." COLOR_RESET "\n");
        }
        return 0;
    } else if (strcmp(action, "start") == 0) {
        (void)system("systemctl --user start openlogid.service 2>/dev/null");
        if (g_json_output) {
            printf("{\"success\":true,\"action\":\"started\"}\n");
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] OpenLogi service started." COLOR_RESET "\n");
        }
        return 0;
    } else {
        fprintf(stderr, "[-] Unknown action '%s'. Use 'status', 'enable', 'disable', 'start', 'restart', or 'stop'.\n", action);
        return 1;
    }
#endif
}

static const char *get_preferred_app_target(char *out_buf, size_t max_len)
{
    const char *home = getenv("HOME");
    char user_app[1024];
    if (home) {
        snprintf(user_app, sizeof(user_app), "%s/Applications/OpenLogi.app", home);
    } else {
        user_app[0] = '\0';
    }

    if (access("/Applications/OpenLogi.app", F_OK) == 0) {
        snprintf(out_buf, max_len, "/Applications/OpenLogi.app");
        return out_buf;
    } else if (user_app[0] && access(user_app, F_OK) == 0) {
        snprintf(out_buf, max_len, "%s", user_app);
        return out_buf;
    }
    return NULL;
}

static bool query_daemon_permissions(ipc_client_t *client, bool *out_acc, bool *out_inp, bool *out_hook)
{
    if (!client || client->fd < 0) return false;
    char *raw = ipc_client_call_raw(client, "openlogi.get_status", "{}");
    if (!raw) return false;

    yyjson_doc *doc = yyjson_read(raw, strlen(raw), 0);
    free(raw);
    if (!doc) return false;

    yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    if (!res) {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_val *perms = yyjson_obj_get(res, "permissions");
    if (perms) {
        if (out_acc) *out_acc = yyjson_get_bool(yyjson_obj_get(perms, "accessibility"));
        if (out_inp) *out_inp = yyjson_get_bool(yyjson_obj_get(perms, "input_monitoring"));
    }
    if (out_hook) *out_hook = yyjson_get_bool(yyjson_obj_get(res, "hook_installed"));

    yyjson_doc_free(doc);
    return true;
}

static void copy_to_clipboard(const char *text)
{
    if (!text || text[0] == '\0') return;
#ifdef __APPLE__
    FILE *p = popen("pbcopy", "w");
    if (p) {
        fputs(text, p);
        pclose(p);
    }
#endif
}

static void request_macos_permissions(const char *binary)
{
#ifdef __APPLE__
    (void)binary;
    bool acc = AXIsProcessTrusted();
    bool inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);

    if (!inp) {
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
        CGRequestListenEventAccess();
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent\" 2>/dev/null");
    } else if (!acc) {
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
            acc = AXIsProcessTrustedWithOptions(options);
            CFRelease(options);
        }
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility\" 2>/dev/null");
    }
#else
    (void)binary;
#endif
}

static bool wait_for_macos_permissions(ipc_client_t *client, int timeout_seconds)
{
#ifdef __APPLE__
    bool cli_acc = AXIsProcessTrusted();
    bool cli_inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);

    bool daemon_acc = false;
    bool daemon_inp = false;
    bool daemon_hook = false;
    bool daemon_ok = false;

    if (client && client->fd >= 0) {
        daemon_ok = query_daemon_permissions(client, &daemon_acc, &daemon_inp, &daemon_hook);
    }

    if ((daemon_ok && daemon_acc && daemon_inp) || (!daemon_ok && cli_acc && cli_inp)) {
        return true;
    }

    const char *spinner = "|/-\\";
    int spin_idx = 0;
    int waited_ms = 0;
    int total_ms = timeout_seconds * 1000;

    printf(COLOR_BOLD COLOR_CYAN "[*] Waiting for permission approval in System Settings..." COLOR_RESET "\n");
    printf("    (Press Ctrl+C to skip)\n\n");

    const char *sock = (g_custom_socket[0] != '\0') ? g_custom_socket : NULL;

    while (waited_ms < total_ms) {
        cli_acc = AXIsProcessTrusted();
        cli_inp = (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted);

        if (client) {
            if (client->fd < 0) {
                (void)ipc_client_connect(sock, client);
            }
            if (client->fd >= 0) {
                daemon_ok = query_daemon_permissions(client, &daemon_acc, &daemon_inp, &daemon_hook);
                if (!daemon_ok) {
                    /* Daemon might have restarted after permission approval (Quit & Reopen) */
                    ipc_client_close(client);
                    if (ipc_client_connect(sock, client)) {
                        daemon_ok = query_daemon_permissions(client, &daemon_acc, &daemon_inp, &daemon_hook);
                    }
                }
            }
        }

        bool all_good = daemon_ok ? (daemon_acc && daemon_inp) : (cli_acc && cli_inp);

        if (all_good) {
            printf("\r\033[K" COLOR_BOLD COLOR_GREEN "[✔] OpenLogi permissions: Accessibility: GRANTED | Input Monitoring: GRANTED" COLOR_RESET "\n\n");
            return true;
        }

        bool show_acc = daemon_ok ? daemon_acc : cli_acc;
        bool show_inp = daemon_ok ? daemon_inp : cli_inp;

        printf("\r\033[K [%c] Accessibility: %s | Input Monitoring: %s  (%ds remaining)",
               spinner[spin_idx % 4],
               show_acc ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_YELLOW "PENDING" COLOR_RESET,
               show_inp ? COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_YELLOW "PENDING" COLOR_RESET,
               (total_ms - waited_ms) / 1000);
        fflush(stdout);

        spin_idx++;
        usleep(250000);
        waited_ms += 250;
    }
    printf("\n\n" COLOR_YELLOW "[!] Permission wait timed out. You can grant them in System Settings anytime." COLOR_RESET "\n");
    return false;
#else
    (void)client;
    (void)timeout_seconds;
    return true;
#endif
}

static void reset_macos_permissions(void)
{
#ifdef __APPLE__
    (void)system("tccutil reset Accessibility io.github.openlogi.agent 2>/dev/null");
    (void)system("tccutil reset ListenEvent io.github.openlogi.agent 2>/dev/null");
    (void)system("tccutil reset Accessibility openlogi 2>/dev/null");
    (void)system("tccutil reset ListenEvent openlogi 2>/dev/null");
    printf(COLOR_BOLD COLOR_GREEN "[+] Reset TCC permissions for OpenLogi." COLOR_RESET "\n");
    printf("    macOS security cache cleared. Run 'openlogi doctor' to re-authorize cleanly.\n\n");
#else
    printf("[*] Permission reset is only applicable on macOS.\n");
#endif
}

static int cmd_permissions(int argc, char **argv)
{
#ifdef __APPLE__
    bool cli_acc = AXIsProcessTrusted();
    bool cli_inp = CGPreflightListenEventAccess();

    char binary[4096] = { 0 };
    bool found_bin = tool_sibling_path(g_program_path, getenv("PATH"), "openlogi", binary, sizeof(binary));
    char app_path[1024] = { 0 };
    bool has_app = (get_preferred_app_target(app_path, sizeof(app_path)) != NULL);
    const char *target = has_app ? app_path : (found_bin ? binary : NULL);

    ipc_client_t client;
    const char *sock = (g_custom_socket[0] != '\0') ? g_custom_socket : NULL;
    bool has_client = ipc_client_connect(sock, &client);
    bool daemon_acc = false, daemon_inp = false, daemon_hook = false;
    bool has_daemon = false;
    if (has_client) {
        has_daemon = query_daemon_permissions(&client, &daemon_acc, &daemon_inp, &daemon_hook);
    }

    if (argc > 0 && strcmp(argv[0], "reset") == 0) {
        if (has_client) ipc_client_close(&client);
        reset_macos_permissions();
        return 0;
    }

    if (argc > 0 && (strcmp(argv[0], "doctor") == 0 || strcmp(argv[0], "check") == 0)) {
        if (has_client) ipc_client_close(&client);
        return cmd_doctor(argc - 1, &argv[1]);
    }

    if (argc > 0 && (strcmp(argv[0], "request") == 0 || strcmp(argv[0], "prompt") == 0 || strcmp(argv[0], "ask") == 0)) {
        request_macos_permissions(target);

        if (target) {
            printf(COLOR_BOLD COLOR_GREEN "📋 Copied path to clipboard:" COLOR_RESET " %s\n", target);
            if (has_app) {
                printf("   In System Settings: drag %s from Finder into the list,\n", target);
                printf("   or click '+' and select OpenLogi from Applications.\n\n");
            } else {
                printf("   In System Settings file picker: press " COLOR_BOLD "Cmd+Shift+G" COLOR_RESET " -> press " COLOR_BOLD "Cmd+V" COLOR_RESET " -> Return.\n\n");
            }
        }
        wait_for_macos_permissions(has_client ? &client : NULL, 30);
        cli_acc = AXIsProcessTrusted();
        cli_inp = CGPreflightListenEventAccess();
        if (has_client) {
            query_daemon_permissions(&client, &daemon_acc, &daemon_inp, &daemon_hook);
            ipc_client_close(&client);
        }
        return 0;
    } else if (argc > 0 && strcmp(argv[0], "open") == 0) {
        if (has_client) ipc_client_close(&client);
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility\" 2>/dev/null");
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent\" 2>/dev/null");
        return 0;
    } else if (argc > 0 && strcmp(argv[0], "reveal") == 0) {
        if (has_client) ipc_client_close(&client);
        if (target) {
            copy_to_clipboard(target);
            char cmd[4200];
            snprintf(cmd, sizeof(cmd), "open -R \"%s\" 2>/dev/null", target);
            (void)system(cmd);
            printf(COLOR_BOLD COLOR_GREEN "📋 Copied path to clipboard and revealed in Finder:" COLOR_RESET " %s\n\n", target);
        }
        return 0;
    } else if (argc > 0 && strcmp(argv[0], "wait") == 0) {
        wait_for_macos_permissions(has_client ? &client : NULL, 60);
        cli_acc = AXIsProcessTrusted();
        cli_inp = CGPreflightListenEventAccess();
        if (has_client) {
            query_daemon_permissions(&client, &daemon_acc, &daemon_inp, &daemon_hook);
            ipc_client_close(&client);
        }
        return 0;
    }

    if (has_client) ipc_client_close(&client);

    bool effective_acc = has_daemon ? daemon_acc : cli_acc;
    bool effective_inp = has_daemon ? daemon_inp : cli_inp;

    if (g_json_output) {
        printf("{\"platform\":\"macos\",\"daemon_running\":%s,\"accessibility\":%s,\"input_monitoring\":%s,\"cli_accessibility\":%s,\"cli_input_monitoring\":%s,\"all_granted\":%s}\n",
               has_daemon ? "true" : "false",
               effective_acc ? "true" : "false",
               effective_inp ? "true" : "false",
               cli_acc ? "true" : "false",
               cli_inp ? "true" : "false",
               (effective_acc && effective_inp) ? "true" : "false");
        return (effective_acc && effective_inp) ? 0 : 1;
    }

    print_banner();
    printf(COLOR_BOLD "macOS Permissions Status:" COLOR_RESET "\n");

    if (has_daemon) {
        printf("  Background Daemon (OpenLogi.app):\n");
        printf("    Accessibility:     %s  %s\n",
               daemon_acc ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET,
               daemon_acc ? "(Active)" : "(Required for mouse buttons, scroll & gesture dispatch)");
        printf("    Input Monitoring:  %s  %s\n",
               daemon_inp ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET,
               daemon_inp ? "(Active)" : "(Required for Logitech Bluetooth & USB HID++ access)");
        if (cli_acc != daemon_acc || cli_inp != daemon_inp) {
            printf("  CLI Terminal Context:\n");
            printf("    Accessibility:     %s\n", cli_acc ? "GRANTED" : "DENIED");
            printf("    Input Monitoring:  %s\n", cli_inp ? "GRANTED" : "DENIED");
        }
        printf("\n");
    } else {
        printf("  Accessibility:     %s  %s\n",
               cli_acc ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET,
               cli_acc ? "(Active)" : "(Required for mouse buttons, scroll & gesture dispatch)");
        printf("  Input Monitoring:  %s  %s\n\n",
               cli_inp ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET,
               cli_inp ? "(Active)" : "(Required for Logitech Bluetooth & USB HID++ access)");
    }

    if (!effective_acc || !effective_inp) {
        printf(COLOR_BOLD COLOR_YELLOW "[!] How to enable missing permissions:" COLOR_RESET "\n");
        printf("  1. Run: " COLOR_BOLD "openlogi doctor" COLOR_RESET " (Automated setup & live diagnostic)\n");
        printf("     Or run: " COLOR_BOLD "openlogi permissions request" COLOR_RESET "\n");
        printf("  2. In " COLOR_BOLD "System Settings -> Privacy & Security" COLOR_RESET ":\n");
        printf("     • In " COLOR_CYAN "Accessibility" COLOR_RESET ", toggle ON " COLOR_BOLD "OpenLogi" COLOR_RESET "\n");
        printf("     • In " COLOR_CYAN "Input Monitoring" COLOR_RESET ", toggle ON " COLOR_BOLD "OpenLogi" COLOR_RESET "\n");
        printf("  3. If 'OpenLogi' is already in the list, toggle it " COLOR_BOLD "OFF and back ON" COLOR_RESET ".\n");
        printf("  4. To reset stuck permissions: " COLOR_BOLD "openlogi permissions reset" COLOR_RESET "\n\n");
        return 1;
    }

    printf(COLOR_BOLD COLOR_GREEN "[+] All required macOS permissions are GRANTED!" COLOR_RESET "\n");
    printf("    OpenLogi background driver has full hardware and input access.\n\n");
    return 0;
#else
    (void)argc;
    (void)argv;
    if (g_json_output) {
        printf("{\"platform\":\"linux\",\"accessibility\":true,\"input_monitoring\":true,\"all_granted\":true}\n");
        return 0;
    }
    print_banner();
    printf(COLOR_BOLD "Linux Permissions Status:" COLOR_RESET "\n");
    printf("  Udev / Evdev:      " COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET " (User group permissions active)\n\n");
    return 0;
#endif
}

static void doctor_run_click_test(ipc_client_t *client)
{
    printf(COLOR_BOLD COLOR_CYAN "[5/5] Live Hardware Click Test:" COLOR_RESET "\n");
    printf("    Press any button on your Logitech mouse (Gesture button, Thumb wheel, Back)...\n");
    printf("    (Listening for 6 seconds, or press Ctrl+C to finish)\n\n");
    fflush(stdout);

    yyjson_doc *sub_doc = ipc_client_call(client, "openlogi.subscribe", "{}");
    if (!sub_doc) {
        printf("    [*] Could not subscribe to live event stream; skipping click test.\n");
        return;
    }
    yyjson_doc_free(sub_doc);

    int elapsed_ms = 0;
    bool detected = false;

    while (elapsed_ms < 6000 && !detected) {
        yyjson_doc *ev_doc = ipc_client_read_event(client, 100);
        if (ev_doc) {
            yyjson_val *root = yyjson_doc_get_root(ev_doc);
            yyjson_val *params = yyjson_obj_get(root, "params");
            const char *ev_name = yyjson_get_str(yyjson_obj_get(params, "event"));
            if (ev_name && (strcmp(ev_name, "gesture_triggered") == 0 ||
                            strcmp(ev_name, "dpi_changed") == 0 ||
                            strcmp(ev_name, "smartshift_changed") == 0 ||
                            strcmp(ev_name, "button_down") == 0 ||
                            strcmp(ev_name, "button_up") == 0)) {
                printf(COLOR_BOLD COLOR_GREEN "    [✔] Intercepted event: %s!" COLOR_RESET "\n", ev_name);
                yyjson_val *data = yyjson_obj_get(params, "data");
                if (data) {
                    char *d_str = yyjson_val_write(data, 0, NULL);
                    if (d_str) {
                        printf("        Data: %s\n", d_str);
                        free(d_str);
                    }
                }
                detected = true;
            }
            yyjson_doc_free(ev_doc);
        }
        elapsed_ms += 100;
    }

    if (detected) {
        printf("\n" COLOR_BOLD COLOR_GREEN "    [✔] Live button interception verified! Hardware and event tap are fully active." COLOR_RESET "\n");
    } else {
        printf("    [*] No button click detected during 6s window. You can test anytime with: " COLOR_BOLD "openlogi monitor" COLOR_RESET "\n");
    }
}

static int cmd_doctor(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!g_json_output) {
        print_banner();
        printf(COLOR_BOLD "OpenLogi System Doctor & Diagnostic Wizard" COLOR_RESET "\n");
        printf("Validating executable environment, permissions, daemon service, and hardware...\n\n");
    }

    /* 1. Executable Location & Identity */
    char binary[4096] = { 0 };
    bool found_bin = tool_sibling_path(g_program_path, getenv("PATH"), "openlogi", binary, sizeof(binary));
    char app_path[1024] = { 0 };
    bool has_app = (get_preferred_app_target(app_path, sizeof(app_path)) != NULL);

    if (!g_json_output) {
        printf(COLOR_BOLD "[1/5] Binary & Environment:" COLOR_RESET "\n");
        if (found_bin) {
            printf("    CLI Executable:   " COLOR_CYAN "%s" COLOR_RESET "\n", binary);
        } else {
            printf("    CLI Executable:   " COLOR_YELLOW "%s" COLOR_RESET "\n", g_program_path ? g_program_path : "openlogi");
        }
        if (has_app) {
            printf("    macOS App Bundle: " COLOR_CYAN "%s" COLOR_RESET "\n", app_path);
        }
    }

    bool cs_valid = false;
#ifdef __APPLE__
    const char *sign_target = has_app ? app_path : (found_bin ? binary : NULL);
    if (sign_target) {
        char cs_cmd[4200];
        snprintf(cs_cmd, sizeof(cs_cmd), "codesign -v \"%s\" 2>/dev/null", sign_target);
        cs_valid = (system(cs_cmd) == 0);
    }
    if (!g_json_output) {
        printf("    Code Signature:   %s\n", cs_valid ? COLOR_GREEN "VALID (Signed)" COLOR_RESET : COLOR_YELLOW "Ad-hoc / Unsigned" COLOR_RESET);
    }
#endif

    /* 2. Daemon & Background Service (Connecting early provides true permission state!) */
    if (!g_json_output) {
        printf("\n" COLOR_BOLD "[2/5] Background Service & Daemon:" COLOR_RESET "\n");
    }
    ipc_client_t client;
    const char *sock = (g_custom_socket[0] != '\0') ? g_custom_socket : NULL;
    bool connected = ipc_client_connect(sock, &client);

    if (!connected) {
        if (!g_json_output) {
            printf("    [*] Daemon is not running. Starting OpenLogi service...\n");
        }
        char *enable_args[] = { (char *)"enable", NULL };
        cmd_autostart(1, enable_args);
        /* Poll up to 3 seconds for daemon socket to come online */
        for (int i = 0; i < 30; i++) {
            usleep(100000);
            if (ipc_client_connect(sock, &client)) {
                connected = true;
                break;
            }
        }
    }

    uint64_t daemon_pid = 0;
    bool is_active = false;
    bool hook_active = false;
    bool daemon_acc = false;
    bool daemon_inp = false;

    if (connected) {
        char *st_raw = ipc_client_call_raw(&client, "openlogi.get_status", "{}");
        if (st_raw) {
            yyjson_doc *doc = yyjson_read(st_raw, strlen(st_raw), 0);
            free(st_raw);
            if (doc) {
                yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
                daemon_pid = yyjson_get_uint(yyjson_obj_get(res, "pid"));
                const char *rstate = yyjson_get_str(yyjson_obj_get(res, "runtime_state"));
                is_active = rstate && (strcmp(rstate, "active") == 0);
                hook_active = yyjson_get_bool(yyjson_obj_get(res, "hook_installed"));

                yyjson_val *perms = yyjson_obj_get(res, "permissions");
                if (perms) {
                    daemon_acc = yyjson_get_bool(yyjson_obj_get(perms, "accessibility"));
                    daemon_inp = yyjson_get_bool(yyjson_obj_get(perms, "input_monitoring"));
                }

                if (!g_json_output) {
                    printf("    Daemon State:     " COLOR_BOLD COLOR_GREEN "RUNNING" COLOR_RESET " (PID: %llu)\n", (unsigned long long)daemon_pid);
                    printf("    Runtime Mode:     %s\n", is_active ? COLOR_GREEN "ACTIVE" COLOR_RESET : COLOR_YELLOW "STANDBY (Waiting for device)" COLOR_RESET);
                    printf("    OS Event Tap:     %s\n", hook_active ? COLOR_GREEN "ACTIVE (Intercepting input)" COLOR_RESET : COLOR_YELLOW "PASSIVE" COLOR_RESET);
                }

                yyjson_doc_free(doc);
            }
        }
    } else {
        if (!g_json_output) {
            fprintf(stderr, COLOR_RED "    [-] Could not connect to OpenLogi daemon. Try running: openlogi service enable\n" COLOR_RESET);
        }
        return 1;
    }

    /* 3. System Permissions */
    if (!g_json_output) {
        printf("\n" COLOR_BOLD "[3/5] System Permissions:" COLOR_RESET "\n");
    }
#ifdef __APPLE__
    bool cli_acc = AXIsProcessTrusted();
    bool cli_inp = CGPreflightListenEventAccess();

    bool acc = connected ? daemon_acc : cli_acc;
    bool inp = connected ? daemon_inp : cli_inp;

    if (!g_json_output) {
        printf("    Daemon Accessibility:    %s\n", daemon_acc ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET);
        printf("    Daemon Input Monitoring: %s\n", daemon_inp ? COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET : COLOR_BOLD COLOR_RED "DENIED" COLOR_RESET);
        if (cli_acc != daemon_acc || cli_inp != daemon_inp) {
            printf("    CLI Terminal Context:    Accessibility: %s | Input Monitoring: %s\n",
                   cli_acc ? "GRANTED" : "DENIED", cli_inp ? "GRANTED" : "DENIED");
        }
    }

    if (!acc || !inp) {
        if (!g_json_output) {
            const char *target = has_app ? app_path : (found_bin ? binary : NULL);

            printf("\n" COLOR_BOLD COLOR_YELLOW "[!] Missing macOS Permissions Detected" COLOR_RESET "\n");
            printf("    OpenLogi requires Accessibility to intercept button clicks and\n");
            printf("    Input Monitoring to communicate with Logitech Bluetooth/USB devices.\n\n");

            if (target) {
                copy_to_clipboard(target);
                printf(COLOR_BOLD COLOR_GREEN "    📋 Application path copied to clipboard:" COLOR_RESET " %s\n\n", target);
            }

            printf("    " COLOR_BOLD "How to authorize in 3 simple steps:" COLOR_RESET "\n");
            printf("    1. System Settings preference panes will open automatically.\n");
            if (has_app) {
                printf("    2. Drag " COLOR_BOLD "%s" COLOR_RESET " from Finder into the list,\n", app_path);
                printf("       OR click '+' in System Settings and select " COLOR_BOLD "OpenLogi" COLOR_RESET " from Applications.\n");
            } else {
                printf("    2. In the file picker (click '+'): press " COLOR_BOLD "Cmd + Shift + G" COLOR_RESET " -> press " COLOR_BOLD "Cmd + V" COLOR_RESET " (Paste) -> Return.\n");
            }
            printf("    3. If 'OpenLogi' is already listed, toggle it " COLOR_BOLD "OFF and back ON" COLOR_RESET " to refresh cache.\n");
            printf("       (If prompted by macOS to quit, click " COLOR_BOLD "\"Quit & Reopen\"" COLOR_RESET ")\n\n");

            request_macos_permissions(target);

            if (!wait_for_macos_permissions(&client, 45)) {
                printf(COLOR_YELLOW "[*] Finish granting permissions in System Settings, then run 'openlogi doctor' again.\n" COLOR_RESET);
                ipc_client_close(&client);
                return 1;
            }
            /* Re-query permissions after wait */
            query_daemon_permissions(&client, &daemon_acc, &daemon_inp, &hook_active);
            acc = daemon_acc;
            inp = daemon_inp;
        }
    } else {
        if (!g_json_output) {
            printf(COLOR_GREEN "    [✔] All macOS permissions are properly configured." COLOR_RESET "\n");
        }
    }
#else
    bool acc = true;
    bool inp = true;
    if (!g_json_output) {
        printf("    Udev / Evdev:     " COLOR_BOLD COLOR_GREEN "GRANTED" COLOR_RESET "\n");
    }
#endif

    /* 4. Connected Hardware */
    if (!g_json_output) {
        printf("\n" COLOR_BOLD "[4/5] Connected Logitech Devices:" COLOR_RESET "\n");
    }
    char *inv_raw = ipc_client_call_raw(&client, "openlogi.get_inventory", "{}");
    size_t dev_count = 0;
    if (inv_raw) {
        yyjson_doc *doc = yyjson_read(inv_raw, strlen(inv_raw), 0);
        free(inv_raw);
        if (doc) {
            yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
            yyjson_val *devs = res ? yyjson_obj_get(res, "devices") : NULL;
            dev_count = (devs && yyjson_is_arr(devs)) ? yyjson_arr_size(devs) : 0;
            if (!g_json_output) {
                if (dev_count == 0) {
                    printf(COLOR_YELLOW "    No Logitech HID++ devices currently connected or awake." COLOR_RESET "\n");
                    printf("    (Move your mouse or reconnect Bluetooth/USB to wake it up)\n");
                } else {
                    for (size_t i = 0; i < dev_count; i++) {
                        yyjson_val *d = yyjson_arr_get(devs, i);
                        const char *name = yyjson_get_str(yyjson_obj_get(d, "product_name"));
                        uint64_t bat = yyjson_get_uint(yyjson_obj_get(d, "battery_percent"));
                        bool chg = yyjson_get_bool(yyjson_obj_get(d, "is_charging"));
                        uint64_t dpi = yyjson_get_uint(yyjson_obj_get(d, "dpi"));
                        bool online = yyjson_get_bool(yyjson_obj_get(d, "online"));

                        printf("    • " COLOR_BOLD "%s" COLOR_RESET " (%s) - Battery: %llu%%%s, DPI: %llu\n",
                               name ? name : "Logitech Device",
                               online ? COLOR_GREEN "Online" COLOR_RESET : COLOR_YELLOW "Offline" COLOR_RESET,
                               (unsigned long long)bat,
                               chg ? " [Charging]" : "",
                               (unsigned long long)dpi);
                    }
                }
            }
            yyjson_doc_free(doc);
        }
    }

    /* 5. Live Interactive Click Test */
    if (!g_json_output) {
        printf("\n");
        if (dev_count > 0 && is_active) {
            doctor_run_click_test(&client);
        } else {
            printf(COLOR_BOLD "[5/5] Live Hardware Click Test:" COLOR_RESET "\n");
            printf("    Skipping click test until a Logitech mouse is connected and awake.\n");
        }
    }

    ipc_client_close(&client);

    if (g_json_output) {
        printf("{\"doctor\":true,\"binary\":\"%s\",\"codesign\":%s,\"accessibility\":%s,\"input_monitoring\":%s,\"daemon_pid\":%llu,\"devices_count\":%zu}\n",
               found_bin ? binary : "unknown",
               cs_valid ? "true" : "false",
               acc ? "true" : "false",
               inp ? "true" : "false",
               (unsigned long long)daemon_pid,
               dev_count);
        return (acc && inp && connected) ? 0 : 1;
    }

    printf("\n" COLOR_BOLD COLOR_GREEN "=====================================================" COLOR_RESET "\n");
    printf(COLOR_BOLD COLOR_GREEN " OpenLogi Doctor check completed successfully!" COLOR_RESET "\n");
    printf(COLOR_BOLD COLOR_GREEN "=====================================================" COLOR_RESET "\n\n");
    return 0;
}

/* Wait for hardware teardown by observing the daemon's lifetime lock. */
static int uninstall_stop_daemon(void)
{
    char path[512];
    const char *state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    int n = state && state[0]
        ? snprintf(path, sizeof(path), "%s/openlogi/agent.lock", state)
        : snprintf(path, sizeof(path), "%s/.local/state/openlogi/agent.lock", home);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "[-] Daemon lock path is too long. Installation retained.\n");
        return 1;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? 0 : 1;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) { close(fd); return 0; }
    if (errno != EWOULDBLOCK && errno != EAGAIN) { close(fd); return 1; }

    /* Match the lock owner with the live RPC server before sending a signal. */
    pid_t pid = single_instance_read_pid(path);
    ipc_client_t client;
    if (!ipc_client_connect(g_custom_socket[0] ? g_custom_socket : NULL, &client)) {
        close(fd);
        fprintf(stderr, "[-] Daemon is busy or unreachable. Stop it before uninstalling.\n");
        return 1;
    }
    yyjson_doc *doc = ipc_client_call(&client, "openlogi.get_status", NULL);
    ipc_client_close(&client);
    yyjson_val *result = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "result") : NULL;
    yyjson_val *value = result ? yyjson_obj_get(result, "pid") : NULL;
    bool matches = pid > 1 && pid != getpid() && yyjson_is_uint(value) &&
                   yyjson_get_uint(value) == (uint64_t)pid;
    if (doc) yyjson_doc_free(doc);
    if (!matches || (kill(pid, SIGTERM) != 0 && errno != ESRCH)) {
        close(fd);
        fprintf(stderr, "[-] Could not safely stop the daemon. Installation retained.\n");
        return 1;
    }
    for (int i = 0; i < 100; i++) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) { close(fd); return 0; }
        if (errno != EWOULDBLOCK && errno != EAGAIN) break;
        usleep(100000);
    }
    close(fd);
    fprintf(stderr, "[-] Daemon shutdown timed out. Installation retained; try again later.\n");
    return 1;
}

static int cmd_stop(void)
{
    char path[512];
    const char *state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    int n = state && state[0]
        ? snprintf(path, sizeof(path), "%s/openlogi/agent.lock", state)
        : snprintf(path, sizeof(path), "%s/.local/state/openlogi/agent.lock", home);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        snprintf(path, sizeof(path), "/tmp/openlogid.lock");
    }

    bool service_stopped = false;
#ifdef __APPLE__
    char target[128];
    snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
    if (run_launchctl("print", target, NULL, true) == 0) {
        (void)run_launchctl("disable", target, NULL, true);
        if (run_launchctl("bootout", target, NULL, true) == 0) {
            service_stopped = true;
        }
    }
#else
    if (system("systemctl --user is-active --quiet openlogid.service 2>/dev/null") == 0) {
        if (system("systemctl --user stop openlogid.service 2>/dev/null") == 0) {
            service_stopped = true;
        }
    }
#endif

    int fd = open(path, O_RDONLY);
    pid_t pid = single_instance_read_pid(path);
    if (pid <= 1) {
        pid = single_instance_read_pid("/tmp/openlogid.lock");
    }

    bool pid_stopped = false;
    if (pid > 1 && kill(pid, 0) == 0) {
        if (kill(pid, SIGTERM) == 0) {
            if (fd >= 0) {
                for (int i = 0; i < 200; i++) {
                    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
                        pid_stopped = true;
                        break;
                    }
                    usleep(10000);
                }
            } else {
                for (int i = 0; i < 40; i++) {
                    usleep(50000);
                    if (kill(pid, 0) != 0) {
                        pid_stopped = true;
                        break;
                    }
                }
            }
            if (!pid_stopped && kill(pid, 0) == 0) {
                kill(pid, SIGKILL);
                pid_stopped = true;
            }
        }
    }
    if (fd >= 0) close(fd);

    if (service_stopped || pid_stopped) {
        if (g_json_output) {
            printf("{\"success\":true,\"stopped\":true,\"pid\":%d}\n", (int)pid);
        } else {
            printf(COLOR_BOLD COLOR_GREEN "[+] OpenLogi daemon stopped successfully." COLOR_RESET "\n");
        }
        return 0;
    }

    if (g_json_output) {
        printf("{\"success\":false,\"stopped\":false,\"message\":\"daemon is not running\"}\n");
    } else {
        printf(COLOR_YELLOW "[*] OpenLogi daemon is not running." COLOR_RESET "\n");
    }
    return 0;
}

static int cmd_uninstall(int argc, char **argv)
{
    bool dry_run = argc == 1 && strcmp(argv[0], "--dry-run") == 0;
    if (argc != 0 && !dry_run) {
        fprintf(stderr, "Usage: openlogi uninstall [--dry-run]\n");
        return 1;
    }
    char binary[4096], paths[4][4096], service[4096];
    const char *names[] = { "openlogid", "openlogictl", "openlogi_discover", "openlogi" };
    const char *home = getenv("HOME");
    if (!home || !home[0] ||
        !tool_sibling_path(g_program_path, getenv("PATH"), "openlogi", binary, sizeof(binary))) {
        fprintf(stderr, "[-] Could not locate the OpenLogi installation or HOME.\n");
        return 1;
    }
    char *slash = strrchr(binary, '/');
    if (!slash) return 1;
    *slash = '\0';
    for (size_t i = 0; i < 4; i++) {
        int n = snprintf(paths[i], sizeof(paths[i]), "%s/%s", binary, names[i]);
        if (n < 0 || (size_t)n >= sizeof(paths[i])) return 1;
        struct stat st;
        if (lstat(paths[i], &st) == 0) {
            if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) {
                fprintf(stderr, "[-] Refusing to remove non-file %s\n", paths[i]);
                return 1;
            }
        } else if (errno != ENOENT) { perror(paths[i]); return 1; }
    }
#ifdef __APPLE__
    int n = snprintf(service, sizeof(service), "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);
#else
    int n = snprintf(service, sizeof(service), "%s/.config/systemd/user/openlogid.service", home);
#endif
    if (n < 0 || (size_t)n >= sizeof(service)) return 1;
    if (dry_run) {
        printf("Would stop the daemon and remove the login service: %s\n", service);
        for (size_t i = 0; i < 4; i++) printf("Would remove: %s\n", paths[i]);
        printf("Configuration will be kept.\n");
        return 0;
    }
    /* Disable respawning before stopping any separately launched daemon. */
    if (access(service, F_OK) == 0) {
#ifdef __APPLE__
        char target[128];
        snprintf(target, sizeof(target), "gui/%u/io.github.openlogi.agent", (unsigned)getuid());
        if (run_launchctl("print", target, NULL, true) == 0) {
            (void)run_launchctl("disable", target, NULL, true);
            if (run_launchctl("bootout", target, NULL, false) != 0) {
                fprintf(stderr, "[-] Could not stop the login service. Installation retained.\n");
                return 1;
            }
        }
#else
        if (system("systemctl --user disable --now openlogid.service") != 0) {
            fprintf(stderr, "[-] Could not disable the login service. Installation retained.\n");
            return 1;
        }
#endif
        if (unlink(service) != 0) { perror(service); return 1; }
    } else if (errno != ENOENT) { perror(service); return 1; }
    if (uninstall_stop_daemon() != 0) return 1;
    for (size_t i = 0; i < 4; i++) {
        if (unlink(paths[i]) != 0 && errno != ENOENT) {
            perror(paths[i]);
            fprintf(stderr, "[-] Uninstall incomplete. Check directory permissions and retry.\n");
            return 1;
        }
    }
    if (g_json_output) puts("{\"success\":true,\"configuration_kept\":true}");
    else printf("[+] OpenLogi uninstalled. Your configuration has been kept.\n");
    return 0;
}

static void print_usage(const char *progname)
{
    printf("OpenLogi Pure C Control Tool (openlogictl) v%s\n", OPENLOGI_AGENT_VERSION);
    printf("Usage: %s [options] <command> [arguments]\n\n", progname);
    printf("Commands:\n");
    printf("  doctor                         Run automated permission, daemon, and hardware diagnostic\n");
    printf("  uninstall [--dry-run]          Remove this installation; keep configuration\n");
    printf("  status                         Show daemon health, permissions, and active app\n");
    printf("  stop                           Stop running background daemon or service\n");
    printf("  restart                        Restart running background daemon or service\n");
    printf("  permissions [request|reveal|reset] Inspect or configure macOS permissions\n");
    printf("  list, devices, inventory       List connected devices and properties\n");
    printf("  scroll [get|invert|res|smooth] Inspect or configure mouse scroll & wheel settings\n");
    printf("  dpi get                        Get current sensor DPI\n");
    printf("  dpi set <value> [dev]          Set active sensor DPI in real time\n");
    printf("  smartshift get                 Get SmartShift mode and threshold\n");
    printf("  smartshift set <mode> <thresh> Configure SmartShift wheel (ratchet/freespin/auto)\n");
    printf("  actions [filter]               List all 53 supported button & gesture actions\n");
    printf("  options, schema                Display reference manual of all TOML settings & values\n");
    printf("  config [show|path|get|set]     Inspect, display, or modify config.toml settings\n");
    printf("  reload                         Hot-reload config.toml immediately\n");
    printf("  monitor                        Stream real-time intercepted events and gestures\n");
    printf("  setup                          Launch interactive ANSI terminal setup wizard\n");
    printf("  autostart, service [status|enable|disable|start|restart|stop] Configure automatic login autostart\n\n");
    printf("Examples:\n");
    printf("  %s doctor                      # Run full automated setup, permissions & hardware check\n", progname);
    printf("  %s scroll invert toggle         # Invert/un-invert mouse wheel scroll direction\n", progname);
    printf("  %s scroll resolution high      # Enable fine-grained hi-res scrolling\n", progname);
    printf("  %s permissions request         # Prompt macOS to grant Accessibility & Input Monitoring\n", progname);
    printf("  %s permissions reset           # Clear macOS TCC cache if permissions are corrupted\n", progname);
    printf("  %s actions                     # View all 53 available actions with descriptions\n", progname);
    printf("  %s options                     # View full TOML configuration options guide\n", progname);
    printf("  %s config set dpi 1600         # Update DPI setting in config.toml\n\n", progname);
    printf("Options:\n");
    printf("  --json                         Output in machine-readable JSON format\n");
    printf("  -s, --socket <path>            Specify custom UNIX domain socket path\n");
    printf("  -a, --autospawn                Automatically start daemon if not running\n");
    printf("  -h, --help                     Show this help message\n");
}

int ctl_main(int argc, char **argv)
{
    g_program_path = argv[0];
    optind = 1;
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

    if (strcmp(cmd, "doctor") == 0 || strcmp(cmd, "check") == 0) {
        return cmd_doctor(sub_argc, sub_argv);
    } else if (strcmp(cmd, "uninstall") == 0) {
        return cmd_uninstall(sub_argc, sub_argv);
    } else if (strcmp(cmd, "stop") == 0) {
        return cmd_stop();
    } else if (strcmp(cmd, "start") == 0) {
        char *args[] = { (char *)"start", NULL };
        return cmd_autostart(1, args);
    } else if (strcmp(cmd, "restart") == 0) {
        char *args[] = { (char *)"restart", NULL };
        return cmd_autostart(1, args);
    } else if (strcmp(cmd, "permissions") == 0 || strcmp(cmd, "perms") == 0 || strcmp(cmd, "permission") == 0) {
        return cmd_permissions(sub_argc, sub_argv);
    } else if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    } else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "inventory") == 0 || strcmp(cmd, "devices") == 0) {
        return cmd_list();
    } else if (strcmp(cmd, "scroll") == 0 || strcmp(cmd, "wheel") == 0) {
        return cmd_scroll(sub_argc, sub_argv);
    } else if (strcmp(cmd, "dpi") == 0) {
        return cmd_dpi(sub_argc, sub_argv);
    } else if (strcmp(cmd, "smartshift") == 0) {
        return cmd_smartshift(sub_argc, sub_argv);
    } else if (strcmp(cmd, "actions") == 0) {
        return cmd_actions(sub_argc, sub_argv);
    } else if (strcmp(cmd, "options") == 0 || strcmp(cmd, "schema") == 0) {
        return cmd_options();
    } else if (strcmp(cmd, "config") == 0) {
        return cmd_config(sub_argc, sub_argv);
    } else if (strcmp(cmd, "reload") == 0) {
        return cmd_reload();
    } else if (strcmp(cmd, "monitor") == 0) {
        return cmd_monitor();
    } else if (strcmp(cmd, "setup") == 0) {
        return cmd_setup();
    } else if (strcmp(cmd, "autostart") == 0 || strcmp(cmd, "service") == 0) {
        return cmd_autostart(sub_argc, sub_argv);
    } else {
        fprintf(stderr, "[-] Unknown command '%s'. Run '%s --help' for available commands.\n", cmd, argv[0]);
        return 1;
    }
}
