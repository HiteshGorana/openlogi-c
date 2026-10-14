/**
 * @file action_queue.h
 * @brief Asynchronous non-blocking action execution queue.
 */

#ifndef OPENLOGI_ACTION_QUEUE_H
#define OPENLOGI_ACTION_QUEUE_H

#include "inject.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the background action execution queue and start worker thread.
 * @return true on success, false on error.
 */
bool action_queue_init(void);

/**
 * @brief Enqueue an action to be executed asynchronously on the background worker thread.
 * Performs a deep copy of the action (including workflow if any).
 * @param action The action to enqueue.
 * @return true if successfully queued, false if queue is full or allocation failed.
 */
bool action_queue_enqueue(const openlogi_action_t *action);

/**
 * @brief Determine if an action should be offloaded to the async worker thread.
 * Returns true for blocking/slow actions: ACTION_WORKFLOW, ACTION_SHELL_COMMAND,
 * ACTION_APP_OPEN, ACTION_APPLE_SCRIPT, ACTION_SLEEP.
 */
bool action_queue_is_blocking(const openlogi_action_t *action);

/**
 * @brief Return number of pending actions currently waiting in queue.
 */
size_t action_queue_pending_count(void);

/**
 * @brief Block until all queued actions have completed execution.
 */
void action_queue_wait_idle(void);

/**
 * @brief Stop worker thread and free all pending actions in queue.
 */
void action_queue_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_ACTION_QUEUE_H */
