/**
 * @file single_instance.c
 * @brief Implementation of single-instance daemon lock with POSIX flock.
 */

#include "single_instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>

static bool mkdir_p(const char *dir_path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    if (len == 0) return false;

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0755);
    return true;
}

bool single_instance_get_default_path(char *out_path, size_t max_len)
{
    if (!out_path || max_len == 0) return false;

    const char *xdg_state = getenv("XDG_STATE_HOME");
    if (xdg_state && xdg_state[0] != '\0') {
        snprintf(out_path, max_len, "%s/openlogi/agent.lock", xdg_state);
        return true;
    }

    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(out_path, max_len, "%s/.local/state/openlogi/agent.lock", home);
        return true;
    }

    snprintf(out_path, max_len, "/tmp/openlogi_agent.lock");
    return true;
}

int single_instance_acquire(const char *lock_path, pid_t *out_existing_pid)
{
    char path[512];
    if (lock_path && lock_path[0] != '\0') {
        strncpy(path, lock_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!single_instance_get_default_path(path, sizeof(path))) {
            return -1;
        }
    }

    /* Ensure parent directory exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir_p(dir);
    }

    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return -1;
    }

    /* Set FD_CLOEXEC so child processes don't inherit lock descriptor */
    int flags = fcntl(fd, F_GETFD);
    if (flags != -1) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            /* Already locked by another instance */
            if (out_existing_pid) {
                *out_existing_pid = single_instance_read_pid(path);
            }
        }
        close(fd);
        return -1;
    }

    /* Lock acquired successfully: write our PID */
    if (ftruncate(fd, 0) == 0) {
        lseek(fd, 0, SEEK_SET);
        char pid_buf[32];
        int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", (int)getpid());
        if (len > 0) {
            ssize_t written = write(fd, pid_buf, (size_t)len);
            (void)written;
            fsync(fd);
        }
    }

    return fd;
}

bool single_instance_update_pid(int lock_fd, pid_t new_pid)
{
    if (lock_fd < 0) return false;

    if (ftruncate(lock_fd, 0) != 0) return false;
    lseek(lock_fd, 0, SEEK_SET);

    char pid_buf[32];
    int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", (int)new_pid);
    if (len <= 0) return false;

    ssize_t written = write(lock_fd, pid_buf, (size_t)len);
    fsync(lock_fd);
    return (written == len);
}

void single_instance_release(int lock_fd, const char *lock_path)
{
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }

    char path[512];
    if (lock_path && lock_path[0] != '\0') {
        strncpy(path, lock_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!single_instance_get_default_path(path, sizeof(path))) {
            return;
        }
    }

    unlink(path);
}

pid_t single_instance_read_pid(const char *lock_path)
{
    char path[512];
    if (lock_path && lock_path[0] != '\0') {
        strncpy(path, lock_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!single_instance_get_default_path(path, sizeof(path))) {
            return 0;
        }
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    char buf[32];
    ssize_t bytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (bytes <= 0) return 0;
    buf[bytes] = '\0';

    long pid_val = strtol(buf, NULL, 10);
    if (pid_val <= 0) return 0;

    return (pid_t)pid_val;
}
