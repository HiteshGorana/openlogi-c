/**
 * @file config_watch.h
 * @brief File modification watcher for instant hot-reload of config.toml.
 */

#ifndef OPENLOGI_CONFIG_WATCH_H
#define OPENLOGI_CONFIG_WATCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*config_reload_cb_t)(const char *filepath, void *user_data);

/**
 * @brief Start background file watcher (kqueue on macOS / inotify on Linux).
 */
bool config_watch_start(const char *filepath, config_reload_cb_t callback, void *user_data);

/**
 * @brief Stop the background file watcher.
 */
void config_watch_stop(void);

/**
 * @brief Check if file watcher is running.
 */
bool config_watch_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_CONFIG_WATCH_H */
