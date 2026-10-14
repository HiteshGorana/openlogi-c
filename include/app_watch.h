/**
 * @file app_watch.h
 * @brief Foreground application monitoring for per-app profiles.
 */

#ifndef OPENLOGI_APP_WATCH_H
#define OPENLOGI_APP_WATCH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*app_watch_cb_t)(const char *app_id, void *user_data);

/**
 * @brief Query the current frontmost application bundle identifier or process name.
 * e.g. "com.apple.Safari", "com.google.Chrome", "com.apple.Terminal".
 * @param out_app_id Buffer to receive the bundle ID or process name.
 * @param max_len Size of buffer.
 * @return true if an app ID was successfully determined, false otherwise.
 */
bool app_watch_get_frontmost(char *out_app_id, size_t max_len);

/**
 * @brief Start monitoring frontmost application changes.
 * Initializes foreground application tracking and invokes callback with initial app.
 * @param callback Callback to invoke on change.
 * @param user_data User data passed to callback.
 * @return true on success, false on error.
 */
bool app_watch_start(app_watch_cb_t callback, void *user_data);

/**
 * @brief Periodically check if frontmost application has changed.
 * Should be called from the daemon's main event loop (e.g. every 100ms).
 * Invokes the registered callback if the frontmost app changed.
 */
void app_watch_tick(void);

/**
 * @brief Stop monitoring application changes.
 */
void app_watch_stop(void);

/**
 * @brief Check if application watcher is currently active.
 */
bool app_watch_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_APP_WATCH_H */
