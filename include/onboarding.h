#ifndef OPENLOGI_ONBOARDING_H
#define OPENLOGI_ONBOARDING_H

#include <stdbool.h>

typedef enum {
    ONBOARD_WELCOME, ONBOARD_INPUT, ONBOARD_ACCESSIBILITY, ONBOARD_DRIVER,
    ONBOARD_MOUSE, ONBOARD_LOGIN, ONBOARD_COMPLETE
} onboarding_step_t;

typedef enum {
    ONBOARD_LATER, ONBOARD_CONTINUE, ONBOARD_SETTINGS, ONBOARD_REVEAL,
    ONBOARD_LOGIN_ENABLE, ONBOARD_LOGIN_MANUAL
} onboarding_choice_t;

typedef struct {
    bool completed;
    onboarding_step_t step;
    int login; /* -1: not chosen, 0: manual, 1: start at login */
} onboarding_state_t;

typedef struct {
    bool accessibility;
    bool input_monitoring;
    bool hook;
    bool device;
} onboarding_status_t;

/* Synchronous UI-thread operations. All context and state pointers are borrowed.
 * Permission checks must be fresh; driver() starts the same installed executable
 * and restarts it when requested, without changing the login preference. */
typedef struct {
    bool (*save)(void *, const onboarding_state_t *);
    bool (*permissions)(void *, onboarding_status_t *);
    bool (*request)(void *, onboarding_step_t, bool reveal);
    bool (*driver)(void *, bool restart);
    bool (*status)(void *, onboarding_status_t *);
    bool (*autostart)(void *, bool enable);
    onboarding_choice_t (*prompt)(void *, onboarding_step_t, bool failed);
} onboarding_ops_t;

/* 0: completed, 1: deferred, -1: persistence/operation failure. */
int onboarding_run(onboarding_state_t *state, const onboarding_ops_t *ops, void *context);
bool onboarding_state_load(const char *path, onboarding_state_t *state);
bool onboarding_state_save(const char *path, const onboarding_state_t *state);

#ifdef __APPLE__
int macos_onboarding_main(void);
int macos_permission_probe(void);
#endif
#endif
