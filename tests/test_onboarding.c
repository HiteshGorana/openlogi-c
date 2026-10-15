#include "onboarding.h"
#include "test_framework.h"
#include <assert.h>

TEST_SUITE_DECLARE(g_suite);

typedef struct {
    onboarding_status_t permissions;
    onboarding_status_t daemon;
    onboarding_state_t saved;
    onboarding_step_t prompts[24];
    onboarding_choice_t choices[24];
    size_t count, next;
    int requests, starts, restarts, autostarts;
    bool save_ok, start_ok, status_ok, autostart_ok;
    bool login_enabled, break_after_autostart;
} fixture_t;

static fixture_t fixture(void)
{
    fixture_t f = { 0 };
    f.permissions = (onboarding_status_t){ true, true, false, false };
    f.daemon = (onboarding_status_t){ true, true, true, true };
    f.save_ok = f.start_ok = f.status_ok = f.autostart_ok = true;
    return f;
}

static void expect(fixture_t *f, onboarding_step_t step, onboarding_choice_t choice)
{
    assert(f->count < 24);
    f->prompts[f->count] = step;
    f->choices[f->count++] = choice;
}

static bool save(void *context, const onboarding_state_t *state)
{
    fixture_t *f = context;
    if (!f->save_ok) return false;
    f->saved = *state;
    return true;
}
static bool permissions(void *context, onboarding_status_t *status)
{
    *status = ((fixture_t *)context)->permissions;
    return true;
}
static bool request(void *context, onboarding_step_t step, bool reveal)
{
    fixture_t *f = context;
    assert(!f->saved.completed && f->saved.step == step);
    if (!reveal) {
        if (step == ONBOARD_INPUT) f->permissions.input_monitoring = true;
        if (step == ONBOARD_ACCESSIBILITY) f->permissions.accessibility = true;
        f->requests++;
    }
    return true;
}
static bool driver(void *context, bool restart)
{
    fixture_t *f = context;
    f->starts++;
    if (restart) f->restarts++;
    return f->start_ok;
}
static bool status(void *context, onboarding_status_t *out)
{
    fixture_t *f = context;
    *out = f->daemon;
    return f->status_ok;
}
static bool autostart(void *context, bool enable)
{
    fixture_t *f = context;
    assert(f->saved.step == ONBOARD_LOGIN && !f->saved.completed);
    assert(f->saved.login == (int)enable);
    f->autostarts++;
    f->login_enabled = enable;
    if (f->break_after_autostart) f->daemon.hook = false;
    return f->autostart_ok;
}
static onboarding_choice_t prompt(void *context, onboarding_step_t step, bool failed)
{
    (void)failed;
    fixture_t *f = context;
    assert(f->next < f->count);
    assert(f->prompts[f->next] == step);
    return f->choices[f->next++];
}
static const onboarding_ops_t ops = { save, permissions, request, driver, status, autostart, prompt };

static void test_first_run_manual(void)
{
    fixture_t f = fixture();
    f.permissions.accessibility = f.permissions.input_monitoring = false;
    expect(&f, ONBOARD_WELCOME, ONBOARD_CONTINUE);
    expect(&f, ONBOARD_INPUT, ONBOARD_SETTINGS);
    expect(&f, ONBOARD_ACCESSIBILITY, ONBOARD_SETTINGS);
    expect(&f, ONBOARD_LOGIN, ONBOARD_LOGIN_MANUAL);
    onboarding_state_t state = { false, ONBOARD_WELCOME, -1 };
    assert(onboarding_run(&state, &ops, &f) == 0);
    assert(f.requests == 2 && f.restarts == 1);
    assert(f.autostarts == 1 && !f.login_enabled);
    assert(f.saved.completed && f.saved.login == 0);
    assert(f.next == f.count);
}

static void test_resume_with_running_daemon(void)
{
    fixture_t f = fixture(); /* driver is already available */
    f.permissions.accessibility = false;
    expect(&f, ONBOARD_ACCESSIBILITY, ONBOARD_SETTINGS);
    expect(&f, ONBOARD_LOGIN, ONBOARD_LOGIN_ENABLE);
    onboarding_state_t state = { false, ONBOARD_ACCESSIBILITY, -1 };
    assert(onboarding_run(&state, &ops, &f) == 0);
    assert(f.requests == 1 && f.restarts == 1 && f.login_enabled);
}

static void test_quit_and_reopen(void)
{
    fixture_t f = fixture();
    f.permissions.input_monitoring = false;
    expect(&f, ONBOARD_INPUT, ONBOARD_LATER);
    onboarding_state_t state = { false, ONBOARD_INPUT, -1 };
    assert(onboarding_run(&state, &ops, &f) == 1);
    assert(!f.saved.completed && f.saved.step == ONBOARD_INPUT && f.starts == 0);
    state = f.saved;
    f = fixture();
    expect(&f, ONBOARD_LOGIN, ONBOARD_LOGIN_MANUAL);
    assert(onboarding_run(&state, &ops, &f) == 0);
    assert(f.requests == 0 && f.restarts == 1);
}

static void test_daemon_permissions_and_hook_required(void)
{
    for (int missing = 0; missing < 4; missing++) {
        fixture_t f = fixture();
        if (missing == 0) f.daemon.accessibility = false;
        if (missing == 1) f.daemon.input_monitoring = false;
        if (missing == 2) f.daemon.hook = false;
        if (missing == 3) f.status_ok = false;
        expect(&f, ONBOARD_DRIVER, ONBOARD_LATER);
        onboarding_state_t state = { false, ONBOARD_MOUSE, -1 };
        assert(onboarding_run(&state, &ops, &f) == 1);
        assert(!f.saved.completed && f.autostarts == 0);
    }
}

static void test_service_failure_is_not_missing_mouse(void)
{
    fixture_t f = fixture();
    f.start_ok = false;
    expect(&f, ONBOARD_DRIVER, ONBOARD_CONTINUE);
    expect(&f, ONBOARD_DRIVER, ONBOARD_LATER);
    onboarding_state_t state = { false, ONBOARD_DRIVER, -1 };
    assert(onboarding_run(&state, &ops, &f) == 1);
    assert(f.restarts == 2 && f.autostarts == 0 && !f.saved.completed);
}

static void test_missing_mouse_can_resume(void)
{
    fixture_t f = fixture();
    f.daemon.device = f.daemon.hook = false;
    expect(&f, ONBOARD_MOUSE, ONBOARD_CONTINUE);
    expect(&f, ONBOARD_MOUSE, ONBOARD_LATER);
    onboarding_state_t state = { false, ONBOARD_MOUSE, -1 };
    assert(onboarding_run(&state, &ops, &f) == 1);
    assert(f.restarts == 1 && f.starts == 2 && f.saved.step == ONBOARD_MOUSE);
    f = fixture();
    expect(&f, ONBOARD_LOGIN, ONBOARD_LOGIN_ENABLE);
    assert(onboarding_run(&state, &ops, &f) == 0);
}

static void test_failed_completion_is_resumable(void)
{
    for (int failure = 0; failure < 2; failure++) {
        fixture_t f = fixture();
        if (failure == 0) f.autostart_ok = false;
        else f.break_after_autostart = true;
        expect(&f, ONBOARD_LOGIN, ONBOARD_LOGIN_MANUAL);
        onboarding_state_t state = { false, ONBOARD_LOGIN, -1 };
        assert(onboarding_run(&state, &ops, &f) == -1);
        assert(!f.saved.completed && f.saved.login == 0);
        f = fixture();
        /* Saved choice survives failure: do not re-prompt or silently enable login. */
        assert(onboarding_run(&state, &ops, &f) == 0);
        assert(!f.login_enabled && f.next == 0);
    }
}

static void test_persistence_failure_stops_before_side_effects(void)
{
    fixture_t f = fixture();
    f.save_ok = false;
    onboarding_state_t state = { false, ONBOARD_INPUT, -1 };
    assert(onboarding_run(&state, &ops, &f) == -1);
    assert(f.requests == 0 && f.starts == 0 && f.autostarts == 0);
}

static void test_atomic_state_and_legacy_resume(void)
{
    char path[] = "/tmp/openlogi-onboarding-test.XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    onboarding_state_t state = { false, ONBOARD_ACCESSIBILITY, 0 }, loaded;
    assert(onboarding_state_save(path, &state));
    assert(onboarding_state_load(path, &loaded));
    assert(!loaded.completed && loaded.step == ONBOARD_ACCESSIBILITY && loaded.login == 0);
    state = (onboarding_state_t){ true, ONBOARD_COMPLETE, 1 };
    assert(onboarding_state_save(path, &state));
    assert(onboarding_state_load(path, &loaded));
    assert(loaded.completed && loaded.login == 1);
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("completed=1\nstep=completed\n", file);
    fclose(file);
    assert(onboarding_state_load(path, &loaded));
    assert(loaded.completed && loaded.login == -1);
    file = fopen(path, "w");
    assert(file);
    fputs("completed=1\nstep=comp", file);
    fclose(file);
    assert(onboarding_state_load(path, &loaded));
    assert(!loaded.completed);
    state.step = (onboarding_step_t)999;
    assert(!onboarding_state_save(path, &state));
    assert(onboarding_state_load(path, &loaded) && !loaded.completed);
    unlink(path);
}

int main(void)
{
    test_suite_begin(&g_suite, "test_onboarding", "Resumable macOS setup and driver permission verification");
    TEST_CASE_RUN(g_suite, test_first_run_manual);
    TEST_CASE_RUN(g_suite, test_resume_with_running_daemon);
    TEST_CASE_RUN(g_suite, test_quit_and_reopen);
    TEST_CASE_RUN(g_suite, test_daemon_permissions_and_hook_required);
    TEST_CASE_RUN(g_suite, test_service_failure_is_not_missing_mouse);
    TEST_CASE_RUN(g_suite, test_missing_mouse_can_resume);
    TEST_CASE_RUN(g_suite, test_failed_completion_is_resumable);
    TEST_CASE_RUN(g_suite, test_persistence_failure_stops_before_side_effects);
    TEST_CASE_RUN(g_suite, test_atomic_state_and_legacy_resume);
    test_suite_end(&g_suite);
    return g_suite.failed ? 1 : 0;
}
