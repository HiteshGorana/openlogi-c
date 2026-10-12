/**
 * @file signals.c
 * @brief Implementation of POSIX signal handling and hardware cleanup.
 */

#include "signals.h"
#include "hidpp_reprog.h"
#include "hook.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_CLEANUPS 8

static cleanup_callback_t g_cleanups[MAX_CLEANUPS];
static size_t g_cleanup_count = 0;
static volatile sig_atomic_t g_shutting_down = 0;

static void execute_cleanups(void)
{
    if (g_shutting_down) {
        return;
    }
    g_shutting_down = 1;

    /* 1. First disarm all firmware button diversions so mouse hardware is restored */
    hidpp_diversion_teardown_all();

    /* 2. Stop OS event hook */
    hook_stop();

    /* 3. Call any other registered callbacks */
    for (size_t i = 0; i < g_cleanup_count; i++) {
        if (g_cleanups[i]) {
            g_cleanups[i]();
        }
    }
}

static volatile sig_atomic_t g_should_exit = 0;

static void on_signal(int sig)
{
    (void)sig;
    if (g_should_exit) {
        /* Force immediate termination on second signal */
        execute_cleanups();
        _exit(1);
    }
    g_should_exit = 1;
}

bool signals_should_exit(void)
{
    return g_should_exit != 0;
}

void signals_register_cleanup(cleanup_callback_t cleanup)
{
    if (g_cleanup_count < MAX_CLEANUPS && cleanup) {
        g_cleanups[g_cleanup_count++] = cleanup;
    }
}

bool signals_init(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_signal;
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) != 0) return false;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return false;
    if (sigaction(SIGHUP, &sa, NULL) != 0) return false;
    if (sigaction(SIGQUIT, &sa, NULL) != 0) return false;

    /* Ignore SIGPIPE to avoid killing daemon when socket clients disconnect */
    signal(SIGPIPE, SIG_IGN);

    /* Register atexit handler as well */
    atexit(execute_cleanups);

    return true;
}

void signals_teardown(void)
{
    execute_cleanups();
}
