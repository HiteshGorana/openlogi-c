/**
 * @file config_watch.c
 * @brief kqueue-based configuration file modification watcher for macOS/BSD.
 */

#include "config_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/event.h>
#include <sys/time.h>

static pthread_t g_watch_thread;
static volatile bool g_is_running = false;
static int g_kq = -1;
static int g_stop_pipe[2] = { -1, -1 };
static char g_watch_path[512] = { 0 };
static config_reload_cb_t g_callback = NULL;
static void *g_user_data = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void *watch_thread_main(void *arg)
{
    (void)arg;

    int file_fd = open(g_watch_path, O_RDONLY);
    if (file_fd < 0) {
        pthread_mutex_lock(&g_lock);
        g_is_running = false;
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }

    struct kevent change_events[2];
    EV_SET(&change_events[0], file_fd, EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
    EV_SET(&change_events[1], g_stop_pipe[0], EVFILT_READ,
           EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, NULL);

    kevent(g_kq, change_events, 2, NULL, 0, NULL);

    while (g_is_running) {
        struct kevent event;
        struct timespec timeout = { 1, 0 }; /* 1-second tick */
        int nev = kevent(g_kq, NULL, 0, &event, 1, &timeout);

        if (!g_is_running) break;
        if (nev <= 0) continue;

        if (event.ident == (uintptr_t)g_stop_pipe[0]) {
            break; /* Stop signal received */
        }

        if (event.ident == (uintptr_t)file_fd) {
            /* File changed */
            if (g_callback) {
                g_callback(g_watch_path, g_user_data);
            }

            /* If file was replaced/renamed (atomic save), reopen */
            if (event.fflags & (NOTE_DELETE | NOTE_RENAME)) {
                close(file_fd);
                usleep(50000); /* 50ms settling delay */
                file_fd = open(g_watch_path, O_RDONLY);
                if (file_fd >= 0) {
                    struct kevent ev;
                    EV_SET(&ev, file_fd, EVFILT_VNODE,
                           EV_ADD | EV_ENABLE | EV_CLEAR,
                           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
                    kevent(g_kq, &ev, 1, NULL, 0, NULL);
                }
            }
        }
    }

    if (file_fd >= 0) {
        close(file_fd);
    }

    return NULL;
}

bool config_watch_start(const char *filepath, config_reload_cb_t callback, void *user_data)
{
    if (!filepath || !callback) return false;

    pthread_mutex_lock(&g_lock);
    if (g_is_running) {
        pthread_mutex_unlock(&g_lock);
        return true;
    }

    strncpy(g_watch_path, filepath, sizeof(g_watch_path) - 1);
    g_callback = callback;
    g_user_data = user_data;

    if (pipe(g_stop_pipe) != 0) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    g_kq = kqueue();
    if (g_kq < 0) {
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    g_is_running = true;
    if (pthread_create(&g_watch_thread, NULL, watch_thread_main, NULL) != 0) {
        g_is_running = false;
        close(g_kq);
        g_kq = -1;
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    pthread_mutex_unlock(&g_lock);
    return true;
}

void config_watch_stop(void)
{
    pthread_mutex_lock(&g_lock);
    if (!g_is_running) {
        pthread_mutex_unlock(&g_lock);
        return;
    }

    g_is_running = false;
    if (g_stop_pipe[1] >= 0) {
        char stop_byte = 1;
        (void)write(g_stop_pipe[1], &stop_byte, 1);
    }
    pthread_mutex_unlock(&g_lock);

    pthread_join(g_watch_thread, NULL);

    pthread_mutex_lock(&g_lock);
    if (g_kq >= 0) {
        close(g_kq);
        g_kq = -1;
    }
    if (g_stop_pipe[0] >= 0) {
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        g_stop_pipe[0] = -1;
        g_stop_pipe[1] = -1;
    }
    pthread_mutex_unlock(&g_lock);
}

bool config_watch_is_running(void)
{
    return g_is_running;
}

#else
/* Linux inotify fallback or stub */
bool config_watch_start(const char *filepath, config_reload_cb_t callback, void *user_data) {
    (void)filepath; (void)callback; (void)user_data; return false;
}
void config_watch_stop(void) {}
bool config_watch_is_running(void) { return false; }
#endif
