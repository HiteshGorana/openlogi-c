/**
 * @file app_watch.c
 * @brief macOS foreground application monitoring via NSWorkspace and Obj-C runtime.
 */

#ifdef __APPLE__

#include "app_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <objc/runtime.h>
#include <objc/message.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *context);

typedef id (*ObjMsgSend0)(id, SEL);
typedef const char *(*StrMsgSend0)(id, SEL);

static char g_last_app[256] = { 0 };
static volatile bool g_is_running = false;
static app_watch_cb_t g_callback = NULL;
static void *g_user_data = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#include <CoreFoundation/CoreFoundation.h>

bool app_watch_get_frontmost(char *out_app_id, size_t max_len)
{
    if (!out_app_id || max_len == 0) return false;
    out_app_id[0] = '\0';

    void *pool = objc_autoreleasePoolPush();

    Class nsWorkspaceClass = objc_getClass("NSWorkspace");
    if (!nsWorkspaceClass) {
        objc_autoreleasePoolPop(pool);
        return false;
    }

    SEL sharedWorkspaceSel = sel_registerName("sharedWorkspace");
    SEL frontmostAppSel = sel_registerName("frontmostApplication");
    SEL bundleIdSel = sel_registerName("bundleIdentifier");
    SEL execUrlSel = sel_registerName("executableURL");
    SEL pathSel = sel_registerName("path");
    SEL localizedNameSel = sel_registerName("localizedName");

    ObjMsgSend0 sendObj = (ObjMsgSend0)objc_msgSend;

    id workspace = sendObj((id)nsWorkspaceClass, sharedWorkspaceSel);
    if (!workspace) {
        objc_autoreleasePoolPop(pool);
        return false;
    }

    id app = sendObj(workspace, frontmostAppSel);
    if (!app) {
        objc_autoreleasePoolPop(pool);
        return false;
    }

    bool found = false;

    /* 1. Try bundleIdentifier (e.g. "com.apple.Safari") */
    id bundleId = sendObj(app, bundleIdSel);
    if (bundleId && CFGetTypeID(bundleId) == CFStringGetTypeID()) {
        if (CFStringGetCString((CFStringRef)bundleId, out_app_id, max_len, kCFStringEncodingUTF8)) {
            if (out_app_id[0] != '\0') found = true;
        }
    }

    /* 2. Fallback to executableURL -> path */
    if (!found) {
        id execUrl = sendObj(app, execUrlSel);
        if (execUrl) {
            id path = sendObj(execUrl, pathSel);
            if (path && CFGetTypeID(path) == CFStringGetTypeID()) {
                if (CFStringGetCString((CFStringRef)path, out_app_id, max_len, kCFStringEncodingUTF8)) {
                    if (out_app_id[0] != '\0') found = true;
                }
            }
        }
    }

    /* 3. Fallback to localizedName */
    if (!found) {
        id locName = sendObj(app, localizedNameSel);
        if (locName && CFGetTypeID(locName) == CFStringGetTypeID()) {
            if (CFStringGetCString((CFStringRef)locName, out_app_id, max_len, kCFStringEncodingUTF8)) {
                if (out_app_id[0] != '\0') found = true;
            }
        }
    }

    objc_autoreleasePoolPop(pool);
    return found;
}

bool app_watch_get_cached(char *out_app_id, size_t max_len)
{
    if (!out_app_id || max_len == 0) return false;
    out_app_id[0] = '\0';
    pthread_mutex_lock(&g_lock);
    if (g_last_app[0] != '\0') {
        strncpy(out_app_id, g_last_app, max_len - 1);
        out_app_id[max_len - 1] = '\0';
        pthread_mutex_unlock(&g_lock);
        return true;
    }
    pthread_mutex_unlock(&g_lock);
    return false;
}

void app_watch_tick(void)
{
    pthread_mutex_lock(&g_lock);
    if (!g_is_running || !g_callback) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    app_watch_cb_t cb = g_callback;
    void *ud = g_user_data;
    pthread_mutex_unlock(&g_lock);

    char current_app[256] = { 0 };
    if (app_watch_get_frontmost(current_app, sizeof(current_app))) {
        bool changed = false;
        pthread_mutex_lock(&g_lock);
        if (strcmp(current_app, g_last_app) != 0) {
            strncpy(g_last_app, current_app, sizeof(g_last_app) - 1);
            g_last_app[sizeof(g_last_app) - 1] = '\0';
            changed = true;
        }
        pthread_mutex_unlock(&g_lock);

        if (changed && cb) {
            cb(current_app, ud);
        }
    }
}

bool app_watch_start(app_watch_cb_t callback, void *user_data)
{
    if (!callback) return false;

    pthread_mutex_lock(&g_lock);
    if (g_is_running) {
        pthread_mutex_unlock(&g_lock);
        return true;
    }

    g_callback = callback;
    g_user_data = user_data;
    g_is_running = true;
    memset(g_last_app, 0, sizeof(g_last_app));

    /* Initial query and immediate notification */
    if (app_watch_get_frontmost(g_last_app, sizeof(g_last_app))) {
        app_watch_cb_t cb = g_callback;
        void *ud = g_user_data;
        pthread_mutex_unlock(&g_lock);

        if (cb) {
            cb(g_last_app, ud);
        }
        return true;
    }

    pthread_mutex_unlock(&g_lock);
    return true;
}

void app_watch_stop(void)
{
    pthread_mutex_lock(&g_lock);
    g_is_running = false;
    g_callback = NULL;
    g_user_data = NULL;
    g_last_app[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

bool app_watch_is_running(void)
{
    return g_is_running;
}

#else

#include "app_watch.h"

bool app_watch_get_frontmost(char *out_app_id, size_t max_len) {
    (void)out_app_id; (void)max_len; return false;
}
bool app_watch_get_cached(char *out_app_id, size_t max_len) {
    (void)out_app_id; (void)max_len; return false;
}
bool app_watch_start(app_watch_cb_t callback, void *user_data) {
    (void)callback; (void)user_data; return false;
}
void app_watch_tick(void) {}
void app_watch_stop(void) {}
bool app_watch_is_running(void) { return false; }

#endif
