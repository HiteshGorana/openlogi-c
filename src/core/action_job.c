#include "action_job.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void pause_tick(void)
{
    struct timespec t = { 0, 10000000 };
    nanosleep(&t, NULL);
}
static bool expired(bool (*cancelled)(void *), void *context, uint64_t deadline)
{
    return cancelled(context) || now_ms() >= deadline;
}
static bool run_child(char *const argv[], bool (*cancelled)(void *), void *context, uint64_t deadline)
{
    if (expired(cancelled, context, deadline)) return false;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t files;
    if (posix_spawnattr_init(&attr) != 0) return false;
    if (posix_spawn_file_actions_init(&files) != 0) { posix_spawnattr_destroy(&attr); return false; }
    sigset_t defaults, mask;
    sigemptyset(&mask);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM); sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGHUP); sigaddset(&defaults, SIGQUIT); sigaddset(&defaults, SIGPIPE);
    bool ok = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK) == 0 &&
        posix_spawnattr_setpgroup(&attr, 0) == 0 && posix_spawnattr_setsigdefault(&attr, &defaults) == 0 &&
        posix_spawnattr_setsigmask(&attr, &mask) == 0 &&
        posix_spawn_file_actions_addopen(&files, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
        posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
        posix_spawn_file_actions_addopen(&files, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0;
    pid_t child = 0;
    if (ok) ok = posix_spawn(&child, argv[0], &files, &attr, argv, environ) == 0;
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
    if (!ok) return false;
    int status = 0;
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0 && errno != EINTR) return false;
        if (expired(cancelled, context, deadline)) break;
        pause_tick();
    }
    kill(-child, SIGTERM);
    /* Retain the unreaped leader's PID through the grace period so the process
     * group cannot be confused with a reused PID when escalating descendants. */
    uint64_t grace = now_ms() + 100;
    while (now_ms() < grace) pause_tick();
    kill(-child, SIGKILL);
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) break;
    return false;
}
static bool command(action_type_t type, const char *value, bool (*cancelled)(void *),
                    void *context, uint64_t deadline)
{
    char *shell[] = { (char *)"/bin/sh", (char *)"-c", (char *)value, NULL };
    char *script[] = { (char *)"/usr/bin/osascript", (char *)"-e", (char *)value, NULL };
    char *app[] = { (char *)"/usr/bin/open", (char *)(strchr(value, '/') ? "--" : "-b"), (char *)value, NULL };
    char *sleep[] = { (char *)"/usr/bin/pmset", (char *)"sleepnow", NULL };
    switch (type) {
        case ACTION_SHELL_COMMAND: return run_child(shell, cancelled, context, deadline);
        case ACTION_APPLE_SCRIPT: return run_child(script, cancelled, context, deadline);
        case ACTION_APP_OPEN: return run_child(app, cancelled, context, deadline);
        case ACTION_SLEEP: return run_child(sleep, cancelled, context, deadline);
        default: return false;
    }
}

bool action_job_run(const openlogi_action_t *action, bool (*cancelled)(void *),
                    void (*emit)(void *, const openlogi_action_t *), void *context)
{
    uint64_t deadline = now_ms() + ACTION_JOB_TIMEOUT_MS;
    if (!action || expired(cancelled, context, deadline)) return false;
    switch (action->type) {
        case ACTION_SHELL_COMMAND: return command(action->type, action->u.shell_cmd, cancelled, context, deadline);
        case ACTION_APPLE_SCRIPT: return command(action->type, action->u.apple_script, cancelled, context, deadline);
        case ACTION_APP_OPEN: return command(action->type, action->u.app_path, cancelled, context, deadline);
        case ACTION_SLEEP: return command(action->type, "", cancelled, context, deadline);
        case ACTION_WORKFLOW: break;
        default: emit(context, action); return true;
    }
    const openlogi_workflow_t *workflow = action->u.workflow;
    if (!workflow || !workflow->step_count || workflow->step_count > MAX_WORKFLOW_STEPS) return false;
    for (size_t i = 0; i < workflow->step_count; i++) {
        if (expired(cancelled, context, deadline)) return false;
        const workflow_step_t *step = &workflow->steps[i];
        openlogi_action_t output = { 0 };
        switch (step->type) {
            case WORKFLOW_STEP_DELAY: {
                if (step->u.delay_ms > MAX_WORKFLOW_DELAY_MS) return false;
                uint64_t until = now_ms() + step->u.delay_ms;
                while (now_ms() < until) {
                    if (expired(cancelled, context, deadline)) return false;
                    pause_tick();
                }
                break;
            }
            case WORKFLOW_STEP_PRESS_KEY:
                output.type = ACTION_KEY_COMBO;
                output.u.key_combo.keycode = step->u.key.keycode;
                output.u.key_combo.modifiers = step->u.key.modifiers;
                emit(context, &output);
                break;
            case WORKFLOW_STEP_TYPE_TEXT:
                output.type = ACTION_TEXT;
                memcpy(output.u.text, step->u.text, sizeof(output.u.text));
                emit(context, &output);
                break;
            case WORKFLOW_STEP_APPLE_SCRIPT:
                if (!command(ACTION_APPLE_SCRIPT, step->u.script, cancelled, context, deadline)) return false;
                break;
            case WORKFLOW_STEP_SHELL_CMD:
                if (!command(ACTION_SHELL_COMMAND, step->u.cmd, cancelled, context, deadline)) return false;
                break;
            default: return false;
        }
    }
    return true;
}
