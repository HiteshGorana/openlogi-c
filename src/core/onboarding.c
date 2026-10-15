#include "onboarding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const steps[] = {
    "welcome", "input_monitoring", "accessibility", "driver", "mouse", "login", "completed"
};

bool onboarding_state_load(const char *path, onboarding_state_t *state)
{
    *state = (onboarding_state_t){ false, ONBOARD_WELCOME, -1 };
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[128];
    bool completed = false;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "completed=1") == 0) completed = true;
        if (strcmp(line, "login=0") == 0) state->login = 0;
        if (strcmp(line, "login=1") == 0) state->login = 1;
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            if (strncmp(line, "step=", 5) == 0 && strcmp(line + 5, steps[i]) == 0)
                state->step = (onboarding_step_t)i;
        }
    }
    bool ok = !ferror(file);
    fclose(file);
    /* A truncated or unrecognized record must never skip setup. */
    state->completed = ok && completed && state->step == ONBOARD_COMPLETE;
    return ok;
}

bool onboarding_state_save(const char *path, const onboarding_state_t *state)
{
    if (state->step < ONBOARD_WELCOME || state->step > ONBOARD_COMPLETE) return false;
    char temp[1024];
    int n = snprintf(temp, sizeof(temp), "%s.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(temp)) return false;
    int fd = mkstemp(temp);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(temp); return false; }
    bool ok = fprintf(file, "completed=%d\nstep=%s\nlogin=%d\n",
                      state->completed, steps[state->step], state->login) > 0;
    if (fflush(file) != 0 || fsync(fd) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok && rename(temp, path) == 0) return true;
    unlink(temp);
    return false;
}

static bool checkpoint(onboarding_state_t *state, onboarding_step_t step,
                       const onboarding_ops_t *ops, void *context)
{
    state->step = step;
    state->completed = step == ONBOARD_COMPLETE;
    return ops->save(context, state);
}

int onboarding_run(onboarding_state_t *state, const onboarding_ops_t *ops, void *context)
{
    if (state->step == ONBOARD_WELCOME) {
        if (!checkpoint(state, ONBOARD_WELCOME, ops, context)) return -1;
        if (ops->prompt(context, ONBOARD_WELCOME, false) != ONBOARD_CONTINUE) return 1;
    }
    /* Always revalidate permissions, including when resuming after Quit & Reopen. */
    for (onboarding_step_t step = ONBOARD_INPUT; step <= ONBOARD_ACCESSIBILITY; step++) {
        if (!checkpoint(state, step, ops, context)) return -1;
        bool failed = false;
        for (;;) {
            onboarding_status_t status = { 0 };
            bool checked = ops->permissions(context, &status);
            if (checked && (step == ONBOARD_INPUT ? status.input_monitoring : status.accessibility)) break;
            onboarding_choice_t choice = ops->prompt(context, step, failed || !checked);
            if (choice == ONBOARD_LATER) return 1;
            if (choice == ONBOARD_SETTINGS || choice == ONBOARD_REVEAL)
                failed = !ops->request(context, step, choice == ONBOARD_REVEAL);
        }
    }

    if (!checkpoint(state, ONBOARD_DRIVER, ops, context)) return -1;
    /* A fresh driver avoids stale permission caches and an older installed copy. */
    bool started = ops->driver(context, true);
    for (;;) {
        onboarding_status_t status = { 0 };
        bool checked = started && ops->status(context, &status);
        onboarding_step_t step = !checked || !status.accessibility || !status.input_monitoring
            ? ONBOARD_DRIVER : !status.device ? ONBOARD_MOUSE : !status.hook ? ONBOARD_DRIVER : ONBOARD_LOGIN;
        if (step == ONBOARD_LOGIN) break;
        if (!checkpoint(state, step, ops, context)) return -1;
        onboarding_choice_t choice = ops->prompt(context, step, step == ONBOARD_DRIVER);
        if (choice == ONBOARD_LATER) return 1;
        started = ops->driver(context, step == ONBOARD_DRIVER);
    }

    if (!checkpoint(state, ONBOARD_LOGIN, ops, context)) return -1;
    if (state->login < 0) {
        onboarding_choice_t choice = ops->prompt(context, ONBOARD_LOGIN, false);
        if (choice != ONBOARD_LOGIN_ENABLE && choice != ONBOARD_LOGIN_MANUAL) return 1;
        state->login = choice == ONBOARD_LOGIN_ENABLE;
        if (!ops->save(context, state)) return -1;
    }
    /* Never announce completion if the preference couldn't be applied or the
     * restarted driver hasn't acquired the real permissions and input hook. */
    if (!ops->autostart(context, state->login == 1)) return -1;
    onboarding_status_t status = { 0 };
    if (!ops->status(context, &status) || !status.accessibility ||
        !status.input_monitoring || !status.device || !status.hook) return -1;
    return checkpoint(state, ONBOARD_COMPLETE, ops, context) ? 0 : -1;
}
