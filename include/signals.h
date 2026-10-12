/**
 * @file signals.h
 * @brief POSIX signal handlers for graceful shutdown and hardware disarm.
 */

#ifndef OPENLOGI_SIGNALS_H
#define OPENLOGI_SIGNALS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cleanup_callback_t)(void);

/**
 * @brief Register a cleanup function to be called on SIGINT, SIGTERM, SIGHUP, or normal exit.
 */
void signals_register_cleanup(cleanup_callback_t cleanup);

/**
 * @brief Install signal handlers for SIGINT, SIGTERM, SIGHUP, SIGQUIT.
 */
bool signals_init(void);

/**
 * @brief Trigger clean exit manually.
 */
void signals_teardown(void);

/**
 * @brief Check if a termination signal (SIGINT, SIGTERM, SIGHUP, SIGQUIT) was caught.
 */
bool signals_should_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SIGNALS_H */
