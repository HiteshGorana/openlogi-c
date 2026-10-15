#ifdef __APPLE__
#include "onboarding.h"
#include "ipc_client.h"
#include "single_instance.h"
#include "yyjson.h"

#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <mach-o/dyld.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static bool g_activate_alert = true;

static void run_alert(const char *title, const char *msg,
                      const char *btn1, const char *btn2, const char *btn3,
                      char *out_btn, size_t out_sz)
{
    if (out_btn && out_sz > 0) out_btn[0] = '\0';

    Class nsAppClass = objc_getClass("NSApplication");
    if (!nsAppClass) return;

    SEL sharedAppSel = sel_registerName("sharedApplication");
    id app = ((id (*)(id, SEL))objc_msgSend)((id)nsAppClass, sharedAppSel);
    if (!app) return;

    Class nsAlertClass = objc_getClass("NSAlert");
    if (!nsAlertClass) return;

    SEL allocSel = sel_registerName("alloc");
    SEL initSel = sel_registerName("init");
    id alert = ((id (*)(id, SEL))objc_msgSend)(((id (*)(id, SEL))objc_msgSend)((id)nsAlertClass, allocSel), initSel);
    if (!alert) return;

    CFStringRef cfTitle = CFStringCreateWithCString(kCFAllocatorDefault, title ? title : "", kCFStringEncodingUTF8);
    CFStringRef cfMsg = CFStringCreateWithCString(kCFAllocatorDefault, msg ? msg : "", kCFStringEncodingUTF8);

    if (!cfTitle || !cfMsg) {
        if (cfTitle) CFRelease(cfTitle);
        if (cfMsg) CFRelease(cfMsg);
        ((void (*)(id, SEL))objc_msgSend)(alert, sel_registerName("release"));
        return;
    }

    SEL setMessageTextSel = sel_registerName("setMessageText:");
    ((void (*)(id, SEL, id))objc_msgSend)(alert, setMessageTextSel, (id)cfTitle);

    SEL setInformativeTextSel = sel_registerName("setInformativeText:");
    ((void (*)(id, SEL, id))objc_msgSend)(alert, setInformativeTextSel, (id)cfMsg);

    const char *titles[] = { btn1, btn2, btn3 };
    for (size_t i = 0; i < 3; i++) {
        if (!titles[i] || !titles[i][0]) continue;
        CFStringRef button = CFStringCreateWithCString(NULL, titles[i], kCFStringEncodingUTF8);
        if (!button) {
            CFRelease(cfTitle);
            CFRelease(cfMsg);
            ((void (*)(id, SEL))objc_msgSend)(alert, sel_registerName("release"));
            return;
        }
        ((void (*)(id, SEL, id))objc_msgSend)(alert, sel_registerName("addButtonWithTitle:"), (id)button);
        CFRelease(button);
    }

    SEL windowSel = sel_registerName("window");
    id alertWindow = ((id (*)(id, SEL))objc_msgSend)(alert, windowSel);
    if (alertWindow) {
        SEL centerSel = sel_registerName("center");
        ((void (*)(id, SEL))objc_msgSend)(alertWindow, centerSel);
    }

    if (g_activate_alert) {
        ((void (*)(id, SEL, bool))objc_msgSend)(app, sel_registerName("activateIgnoringOtherApps:"), true);
    }
    SEL runModalSel = sel_registerName("runModal");
    long resp = ((long (*)(id, SEL))objc_msgSend)(alert, runModalSel);

    const char *clicked = "";
    if (resp == 1000) clicked = btn1 ? btn1 : "";
    else if (resp == 1001) clicked = btn2 ? btn2 : "";
    else if (resp == 1002) clicked = btn3 ? btn3 : "";

    if (out_btn && out_sz > 0) {
        strncpy(out_btn, clicked, out_sz - 1);
        out_btn[out_sz - 1] = '\0';
    }

    if (cfTitle) CFRelease(cfTitle);
    if (cfMsg) CFRelease(cfMsg);

    SEL releaseSel = sel_registerName("release");
    ((void (*)(id, SEL))objc_msgSend)(alert, releaseSel);
}

typedef struct {
    char executable[PATH_MAX];
    char app[PATH_MAX];
    char state_path[512];
    char login_plist[PATH_MAX];
    char session_plist[PATH_MAX];
    char domain[64];
    char service[128];
    bool requested_input;
    bool requested_accessibility;
} macos_setup_t;

/* Run argument vectors directly: installation paths may contain shell syntax.
 * Pump the main run loop while waiting so Settings/relaunch events are serviced. */
static int run_process(const char *path, char *const args[])
{
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    int rc = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    if (rc == 0) rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    if (rc == 0) rc = posix_spawn(&pid, path, &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;
    int status = 0;
    double deadline = CFAbsoluteTimeGetCurrent() + 15.0;
    for (;;) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) break;
        if (result < 0 && errno != EINTR) return -1;
        if (CFAbsoluteTimeGetCurrent() >= deadline) {
            /* This is our helper child, never the hardware-owning daemon. */
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
            return -1;
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool open_target(const char *target, bool reveal)
{
    char *args[] = { (char *)"open", (char *)(reveal ? "-R" : target),
                     reveal ? (char *)target : NULL, NULL };
    return run_process("/usr/bin/open", args) == 0;
}

static int launchctl(macos_setup_t *setup, const char *verb, bool service, const char *path)
{
    if (strcmp(verb, "kickstart") == 0) {
        char *args[] = { (char *)"launchctl", (char *)"kickstart", (char *)"-k",
                         setup->service, NULL };
        return run_process("/bin/launchctl", args);
    }
    char *args[] = { (char *)"launchctl", (char *)verb,
                     service ? setup->service : setup->domain, (char *)path, NULL };
    return run_process("/bin/launchctl", args);
}

static bool ensure_parent(const char *path)
{
    char parent[PATH_MAX];
    int n = snprintf(parent, sizeof(parent), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(parent)) return false;
    for (char *p = parent + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(parent, 0700) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return true;
}

static bool setup_paths(macos_setup_t *setup)
{
    memset(setup, 0, sizeof(*setup));
    char executable[PATH_MAX];
    uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) != 0 ||
        !realpath(executable, setup->executable)) return false;
    snprintf(setup->app, sizeof(setup->app), "%s", setup->executable);
    char *suffix = strstr(setup->app, ".app/Contents/MacOS/");
    if (suffix) suffix[4] = '\0';

    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    const char *state_home = getenv("XDG_STATE_HOME");
    int n = state_home && state_home[0]
        ? snprintf(setup->state_path, sizeof(setup->state_path), "%s/openlogi/onboarding.state", state_home)
        : snprintf(setup->state_path, sizeof(setup->state_path), "%s/.local/state/openlogi/onboarding.state", home);
    if (n < 0 || (size_t)n >= sizeof(setup->state_path)) return false;
    n = snprintf(setup->login_plist, sizeof(setup->login_plist),
                     "%s/Library/LaunchAgents/io.github.openlogi.agent.plist", home);
    if (n < 0 || (size_t)n >= sizeof(setup->login_plist)) return false;
    n = snprintf(setup->session_plist, sizeof(setup->session_plist), "%s.plist", setup->state_path);
    if (n < 0 || (size_t)n >= sizeof(setup->session_plist)) return false;
    snprintf(setup->domain, sizeof(setup->domain), "gui/%u", (unsigned)getuid());
    snprintf(setup->service, sizeof(setup->service), "%s/io.github.openlogi.agent", setup->domain);
    return ensure_parent(setup->state_path);
}

int macos_permission_probe(void)
{
    /* A fresh process avoids Input Monitoring results cached before approval.
     * Bits are private to the helper protocol; failures use a different code. */
    bool input = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted;
    return (input ? 1 : 0) | (AXIsProcessTrusted() ? 2 : 0);
}

static bool setup_permissions(void *context, onboarding_status_t *status)
{
    macos_setup_t *setup = context;
    char *args[] = { setup->executable, (char *)"--permission-probe", NULL };
    int result = run_process(setup->executable, args);
    if (result < 0 || result > 3) return false;
    status->input_monitoring = (result & 1) != 0;
    status->accessibility = (result & 2) != 0;
    return true;
}

static bool setup_save(void *context, const onboarding_state_t *state)
{
    macos_setup_t *setup = context;
    return onboarding_state_save(setup->state_path, state);
}

static bool setup_request(void *context, onboarding_step_t step, bool reveal)
{
    macos_setup_t *setup = context;
    if (reveal) return open_target(setup->app, true);
    if (step == ONBOARD_INPUT && !setup->requested_input) {
        setup->requested_input = true;
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
    } else if (step == ONBOARD_ACCESSIBILITY && !setup->requested_accessibility) {
        setup->requested_accessibility = true;
        const void *keys[] = { kAXTrustedCheckOptionPrompt };
        const void *values[] = { kCFBooleanTrue };
        CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!options) return false;
        AXIsProcessTrustedWithOptions(options);
        CFRelease(options);
    }
    g_activate_alert = false;
    return open_target(step == ONBOARD_INPUT
        ? "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"
        : "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility", false);
}

static bool setup_status(void *context, onboarding_status_t *status)
{
    (void)context;
    memset(status, 0, sizeof(*status));
    ipc_client_t client;
    if (!ipc_client_connect(NULL, &client)) return false;
    yyjson_doc *doc = ipc_client_call(&client, "openlogi.get_status", "{}");
    ipc_client_close(&client);
    if (!doc) return false;
    yyjson_val *res = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    yyjson_val *perms = yyjson_obj_get(res, "permissions");
    yyjson_val *acc = yyjson_obj_get(perms, "accessibility");
    yyjson_val *inp = yyjson_obj_get(perms, "input_monitoring");
    yyjson_val *hook = yyjson_obj_get(res, "hook_installed");
    yyjson_val *devices = yyjson_obj_get(res, "devices_online");
    bool ok = yyjson_is_bool(acc) && yyjson_is_bool(inp) && yyjson_is_bool(hook) && yyjson_is_uint(devices);
    if (ok) {
        status->accessibility = yyjson_get_bool(acc);
        status->input_monitoring = yyjson_get_bool(inp);
        status->hook = yyjson_get_bool(hook);
        status->device = yyjson_get_uint(devices) > 0;
    }
    yyjson_doc_free(doc);
    return ok;
}

/* Stop gracefully and wait for the lifetime lock, so hardware is undiverted
 * before another daemon starts. Never kill a PID from a stale lock file. */
static bool stop_driver(macos_setup_t *setup)
{
    char lock_path[512];
    if (!single_instance_get_default_path(lock_path, sizeof(lock_path))) return false;
    int fd = open(lock_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 && errno != ENOENT) return false;
    if (launchctl(setup, "print", true, NULL) == 0) {
        (void)launchctl(setup, "disable", true, NULL);
        if (launchctl(setup, "bootout", true, NULL) != 0) {
            if (fd >= 0) close(fd);
            return false;
        }
    }
    if (fd < 0) return true;
    bool signaled = false;
    double deadline = CFAbsoluteTimeGetCurrent() + 10.0;
    do {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) { close(fd); return true; }
        if (errno != EWOULDBLOCK && errno != EAGAIN) break;
        if (!signaled) {
            pid_t pid = single_instance_read_pid(lock_path);
            ipc_client_t client;
            if (ipc_client_connect(NULL, &client)) {
                yyjson_doc *doc = ipc_client_call(&client, "openlogi.get_status", "{}");
                ipc_client_close(&client);
                yyjson_val *res = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "result") : NULL;
                yyjson_val *value = yyjson_obj_get(res, "pid");
                bool matches = pid > 1 && pid != getpid() && yyjson_is_uint(value) &&
                    yyjson_get_uint(value) == (uint64_t)pid;
                if (doc) yyjson_doc_free(doc);
                if (matches) {
                    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) break;
                    signaled = true;
                }
            }
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    } while (CFAbsoluteTimeGetCurrent() < deadline);
    close(fd);
    return false;
}

static bool write_service(macos_setup_t *setup, const char *path)
{
    /* CFPropertyList performs XML escaping for paths containing &, <, etc. */
    CFStringRef executable = CFStringCreateWithCString(NULL, setup->executable, kCFStringEncodingUTF8);
    if (!executable) return false;
    const void *args[] = { executable, CFSTR("daemon") };
    CFArrayRef arguments = CFArrayCreate(NULL, args, 2, &kCFTypeArrayCallBacks);
    const void *keep_keys[] = { CFSTR("SuccessfulExit") };
    const void *keep_values[] = { kCFBooleanFalse };
    CFDictionaryRef keep_alive = CFDictionaryCreate(NULL, keep_keys, keep_values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void *keys[] = { CFSTR("Label"), CFSTR("ProgramArguments"), CFSTR("RunAtLoad"),
                          CFSTR("ProcessType"), CFSTR("StandardOutPath"), CFSTR("StandardErrorPath"), CFSTR("KeepAlive") };
    const void *values[] = { CFSTR("io.github.openlogi.agent"), arguments, kCFBooleanTrue,
                            CFSTR("Interactive"), CFSTR("/tmp/openlogid.stdout.log"), CFSTR("/tmp/openlogid.stderr.log"), keep_alive };
    CFDictionaryRef plist = arguments && keep_alive ? CFDictionaryCreate(NULL, keys, values, 7,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks) : NULL;
    CFDataRef data = plist ? CFPropertyListCreateData(NULL, plist, kCFPropertyListXMLFormat_v1_0, 0, NULL) : NULL;
    bool ok = false;
    char temp[PATH_MAX];
    int n = snprintf(temp, sizeof(temp), "%s.XXXXXX", path);
    if (data && n > 0 && (size_t)n < sizeof(temp) && ensure_parent(path)) {
        int fd = mkstemp(temp);
        if (fd >= 0) {
            FILE *file = fdopen(fd, "wb");
            if (file) {
                size_t length = (size_t)CFDataGetLength(data);
                ok = fwrite(CFDataGetBytePtr(data), 1, length, file) == length;
                if (fflush(file) != 0 || fsync(fd) != 0) ok = false;
                if (fclose(file) != 0) ok = false;
                if (ok) ok = rename(temp, path) == 0;
            } else close(fd);
            if (!ok) unlink(temp);
        }
    }
    if (data) CFRelease(data);
    if (plist) CFRelease(plist);
    if (arguments) CFRelease(arguments);
    if (keep_alive) CFRelease(keep_alive);
    CFRelease(executable);
    return ok;
}

static bool wait_for_driver(macos_setup_t *setup)
{
    double deadline = CFAbsoluteTimeGetCurrent() + 8.0;
    bool connected = false;
    do {
        onboarding_status_t status = { 0 };
        connected = setup_status(setup, &status);
        if (connected && status.hook && status.device) return true;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    } while (CFAbsoluteTimeGetCurrent() < deadline);
    return connected; /* A healthy standby daemon is valid while waiting for a mouse. */
}

static bool start_service(macos_setup_t *setup, const char *path)
{
    if (!write_service(setup, path) || !stop_driver(setup)) return false;
    (void)launchctl(setup, "enable", true, NULL);
    if (launchctl(setup, "kickstart", true, NULL) != 0) {
        (void)launchctl(setup, "bootout", true, NULL);
        (void)launchctl(setup, "enable", true, NULL);
        if (launchctl(setup, "bootstrap", false, path) != 0) return false;
        (void)launchctl(setup, "kickstart", true, NULL);
    }
    return wait_for_driver(setup);
}

static bool setup_driver(void *context, bool restart)
{
    macos_setup_t *setup = context;
    onboarding_status_t status;
    if (!restart && setup_status(setup, &status)) return true;
    /* Preserve an existing login choice; first-run setup only loads a session job. */
    return start_service(setup, access(setup->login_plist, F_OK) == 0
        ? setup->login_plist : setup->session_plist);
}

static bool setup_autostart(void *context, bool enable)
{
    macos_setup_t *setup = context;
    if (!enable && unlink(setup->login_plist) != 0 && errno != ENOENT) return false;
    return start_service(setup, enable ? setup->login_plist : setup->session_plist);
}

static onboarding_choice_t setup_prompt(void *context, onboarding_step_t step, bool failed)
{
    macos_setup_t *setup = context;
    char choice[64] = { 0 };
    if (step == ONBOARD_WELCOME) {
        run_alert("Set Up OpenLogi", "Allow two macOS permissions, connect your Logitech mouse, then choose whether OpenLogi starts at login.\n\nYou can finish later and resume by opening this app again.",
                  "Set Up My Mouse", "Finish Later", NULL, choice, sizeof(choice));
        return strcmp(choice, "Set Up My Mouse") == 0 ? ONBOARD_CONTINUE : ONBOARD_LATER;
    }
    if (step == ONBOARD_INPUT || step == ONBOARD_ACCESSIBILITY) {
        bool input = step == ONBOARD_INPUT;
        bool requested = input ? setup->requested_input : setup->requested_accessibility;
        char message[PATH_MAX + 1024];
        snprintf(message, sizeof(message),
            "%s\n\n%s\n\nIn System Settings > Privacy & Security > %s, enable OpenLogi. If it is missing, click + and select this copy:\n%s\n\nIf macOS asks to Quit & Reopen, accept. Setup will resume. After enabling access, return here and click Check Again.%s",
            input ? "Step 1 of 4: Input Monitoring" : "Step 2 of 4: Accessibility",
            input ? "OpenLogi needs access to receive mouse input." : "OpenLogi needs access to perform your mouse shortcuts and gestures.",
            input ? "Input Monitoring" : "Accessibility", setup->app,
            failed ? "\n\nThe last check or request failed. You can open System Settings manually and check again." : "");
        run_alert(input ? "Allow Mouse Input" : "Allow Mouse Actions", message,
                  requested ? "Check Again" : "Open System Settings",
                  requested ? "More Options" : "Finish Later",
                  requested ? "Finish Later" : "Check Again", choice, sizeof(choice));
        if (strcmp(choice, "More Options") == 0) {
            run_alert("Permission Options", "If OpenLogi is missing, reveal this app in Finder and drag it into the permission list. If access is still denied after an update, remove the old entry with the minus button and add this copy again.",
                      "Open System Settings", "Reveal OpenLogi", "Finish Later", choice, sizeof(choice));
        }
        if (strcmp(choice, "Open System Settings") == 0) return ONBOARD_SETTINGS;
        if (strcmp(choice, "Reveal OpenLogi") == 0) return ONBOARD_REVEAL;
        return strcmp(choice, "Check Again") == 0 ? ONBOARD_CONTINUE : ONBOARD_LATER;
    }
    g_activate_alert = true;
    if (step == ONBOARD_DRIVER) {
        run_alert("Background Driver Needs Attention", "The background driver has not confirmed mouse access yet. Restart Driver will retry with the copy of OpenLogi you opened.\n\nIf both permissions are enabled but this keeps happening, remove OpenLogi from the permission lists, add this copy again, then reopen OpenLogi.\n\nDiagnostics: /tmp/openlogid.stderr.log",
                  "Restart Driver", "Finish Later", NULL, choice, sizeof(choice));
        return strcmp(choice, "Restart Driver") == 0 ? ONBOARD_CONTINUE : ONBOARD_LATER;
    }
    if (step == ONBOARD_MOUSE) {
        run_alert("Connect Your Mouse", "Step 3 of 4: Mouse Detection\n\nTurn on your Logitech mouse and connect it through Bluetooth or its USB receiver, then click Check Again.\n\nSetup will only finish after the driver confirms a connected mouse and an active input hook.",
                  "Check Again", "Finish Later", NULL, choice, sizeof(choice));
        return strcmp(choice, "Check Again") == 0 ? ONBOARD_CONTINUE : ONBOARD_LATER;
    }
    if (step == ONBOARD_LOGIN) {
        run_alert("Mouse Access Verified", "Step 4 of 4: Start at Login\n\nThe background driver has both permissions, a connected mouse, and an active input hook.\n\nShould OpenLogi start automatically when you log in? Start Manually keeps it running for this session; open the app again after your next login.",
                  "Start at Login", "Start Manually", "Finish Later", choice, sizeof(choice));
        if (strcmp(choice, "Start at Login") == 0) return ONBOARD_LOGIN_ENABLE;
        if (strcmp(choice, "Start Manually") == 0) return ONBOARD_LOGIN_MANUAL;
    }
    return ONBOARD_LATER;
}

static int run_setup(macos_setup_t *setup, onboarding_state_t *state)
{
    const onboarding_ops_t ops = { setup_save, setup_permissions, setup_request,
        setup_driver, setup_status, setup_autostart, setup_prompt };
    int result = onboarding_run(state, &ops, setup);
    g_activate_alert = true;
    if (result == 0) {
        run_alert("OpenLogi Is Ready", "OpenLogi is running in the background.\n\nCustomize buttons and gestures in your OpenLogi config.toml file. Open the app again to check status or stop the driver.",
                  "OK", NULL, NULL, NULL, 0);
    } else if (result < 0) {
        run_alert("Setup Needs Another Check", "OpenLogi could not save setup or verify the background driver after applying your login preference.\n\nOpen this app again to retry. Your setup has not been marked complete.\n\nCheck /tmp/openlogid.stderr.log for driver errors.",
                  "OK", NULL, NULL, NULL, 0);
    }
    return result < 0 ? 1 : 0;
}

static bool trash_app(macos_setup_t *setup)
{
    size_t length = strlen(setup->app);
    if (length < 4 || strcmp(setup->app + length - 4, ".app") != 0) return false;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)setup->app, (CFIndex)length, true);
    if (!url) return false;
    id manager = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSFileManager"),
        sel_registerName("defaultManager"));
    bool ok = manager && ((bool (*)(id, SEL, id, id *, id *))objc_msgSend)(manager,
        sel_registerName("trashItemAtURL:resultingItemURL:error:"), (id)url, NULL, NULL);
    CFRelease(url);
    return ok;
}

static int app_main(void)
{
    macos_setup_t setup;
    if (!setup_paths(&setup)) {
        run_alert("Could Not Start Setup", "OpenLogi could not locate its app or create its setup directory. Move the app to Applications and try again.", "OK", NULL, NULL, NULL, 0);
        return 1;
    }
    if (strncmp(setup.executable, "/Volumes/", 9) == 0 || strstr(setup.executable, "/AppTranslocation/")) {
        char choice[64];
        run_alert("Move OpenLogi to Applications", "Drag OpenLogi from the disk image into Applications, then open that installed copy. macOS permissions must be granted to the copy you will keep using.",
                  "Open Applications", "Quit", NULL, choice, sizeof(choice));
        if (strcmp(choice, "Open Applications") == 0) open_target("/Applications", false);
        return 0;
    }
    onboarding_state_t state;
    onboarding_state_load(setup.state_path, &state);
    onboarding_status_t status;
    bool running = setup_status(&setup, &status);
    /* A running daemon is not evidence that setup completed or permissions work. */
    if (state.completed && running && status.accessibility && status.input_monitoring &&
        (!status.device || status.hook)) {
        char choice[64];
        run_alert("OpenLogi Is Running", status.device
            ? "Your Logitech mouse is connected and the input hook is active."
            : "OpenLogi is waiting for a Logitech mouse to connect.",
            "Keep Running", "Stop OpenLogi", "More Options", choice, sizeof(choice));
        if (strcmp(choice, "More Options") == 0) {
            run_alert("OpenLogi Options", "You can re-run setup to check permissions and change the login preference, or remove this app.",
                      "Re-run Setup", "Uninstall", "Cancel", choice, sizeof(choice));
        }
        if (strcmp(choice, "Re-run Setup") == 0) {
            state = (onboarding_state_t){ false, ONBOARD_WELCOME, -1 };
        } else if (strcmp(choice, "Stop OpenLogi") == 0 || strcmp(choice, "Uninstall") == 0) {
            if (!stop_driver(&setup)) {
                run_alert("Driver Is Still Stopping", "OpenLogi could not confirm a safe shutdown. Try again in a moment.", "OK", NULL, NULL, NULL, 0);
                return 1;
            }
            if (strcmp(choice, "Uninstall") == 0) {
                if (unlink(setup.login_plist) != 0 && errno != ENOENT) return 1;
                unlink(setup.session_plist);
                unlink(setup.state_path);
                if (!trash_app(&setup)) {
                    open_target(setup.app, true);
                    run_alert("OpenLogi Stopped", "The driver and login startup have been stopped. Move the selected OpenLogi app to Trash to finish removing it. Your mouse configuration is kept.", "OK", NULL, NULL, NULL, 0);
                    return 1;
                }
                run_alert("OpenLogi Uninstalled", "The driver and login startup have been stopped and this app moved to Trash. Your mouse configuration is kept.", "OK", NULL, NULL, NULL, 0);
            }
            return 0;
        } else return 0;
    } else if (state.completed && !running) {
        char choice[64];
        run_alert("OpenLogi Is Stopped", "Start OpenLogi for this session, or re-run setup to change your login preference.",
                  "Start OpenLogi", "Re-run Setup", "Quit", choice, sizeof(choice));
        if (strcmp(choice, "Start OpenLogi") == 0) {
            if (setup_driver(&setup, false) && setup_status(&setup, &status) &&
                status.accessibility && status.input_monitoring && (!status.device || status.hook)) return 0;
            state.completed = false;
            state.step = ONBOARD_INPUT;
        } else if (strcmp(choice, "Re-run Setup") == 0) {
            state = (onboarding_state_t){ false, ONBOARD_WELCOME, -1 };
        } else return 0;
    }
    return run_setup(&setup, &state);
}

int macos_onboarding_main(void)
{
    id pool = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSAutoreleasePool"), sel_registerName("new"));
    id app = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (!app) {
        if (pool) ((void (*)(id, SEL))objc_msgSend)(pool, sel_registerName("drain"));
        return 1;
    }
    ((void (*)(id, SEL, long))objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 1);
    ((void (*)(id, SEL))objc_msgSend)(app, sel_registerName("finishLaunching"));
    int result = app_main();
    if (pool) ((void (*)(id, SEL))objc_msgSend)(pool, sel_registerName("drain"));
    return result;
}
#endif
