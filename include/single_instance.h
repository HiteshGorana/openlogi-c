/**
 * @file single_instance.h
 * @brief Single-instance lock mechanism using POSIX flock.
 */

#ifndef OPENLOGI_SINGLE_INSTANCE_H
#define OPENLOGI_SINGLE_INSTANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the default lock file path (~/.local/state/openlogi/agent.lock).
 *
 * @param out_path Buffer to store path.
 * @param max_len Maximum length of buffer.
 * @return true on success, false on truncation or error.
 */
bool single_instance_get_default_path(char *out_path, size_t max_len);

/**
 * @brief Attempt to acquire an exclusive non-blocking single-instance lock.
 *
 * If successful, writes current process PID into the file and leaves the fd open.
 * If already locked by another process, reads existing process PID into out_existing_pid.
 *
 * @param lock_path Path to the lock file, or NULL to use the default path.
 * @param out_existing_pid Optional out parameter to receive the PID of the existing holder if locked.
 * @return int File descriptor (>= 0) on success, -1 on failure (already locked or error).
 */
int single_instance_acquire(const char *lock_path, pid_t *out_existing_pid);

/**
 * @brief Update the PID stored in an already acquired lock file.
 *
 * Useful after fork() when the child process becomes the daemon.
 *
 * @param lock_fd File descriptor returned by single_instance_acquire.
 * @param new_pid The PID to write.
 * @return true on success, false on error.
 */
bool single_instance_update_pid(int lock_fd, pid_t new_pid);

/**
 * @brief Release the single-instance lock and remove the lock file.
 *
 * @param lock_fd File descriptor returned by single_instance_acquire.
 * @param lock_path Path to the lock file, or NULL to use default path.
 */
void single_instance_release(int lock_fd, const char *lock_path);

/**
 * @brief Read the PID currently stored in a lock file without locking it.
 *
 * @param lock_path Path to the lock file, or NULL to use default path.
 * @return pid_t The PID found, or 0 if file does not exist / invalid.
 */
pid_t single_instance_read_pid(const char *lock_path);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SINGLE_INSTANCE_H */
