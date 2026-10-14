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
#include <stdlib.h>
#include <stdbool.h>

static void macos_prompt_app_permissions(void)
{
    bool acc = AXIsProcessTrusted();
    bool inp = CGPreflightListenEventAccess();

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
        CGRequestListenEventAccess();
        (void)system("open \"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent\" 2>/dev/null");
    }
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
        macos_prompt_app_permissions();
        return daemon_main(argc, argv);
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
