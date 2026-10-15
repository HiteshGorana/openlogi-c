#ifndef OPENLOGI_ACTION_JOB_H
#define OPENLOGI_ACTION_JOB_H
#include "inject.h"
/* A job owns its child process group. Cancellation/timeout terminates descendants
 * and reaps the direct child. emit() must be bounded and must not retain action. */
#define ACTION_JOB_TIMEOUT_MS 30000
bool action_job_run(const openlogi_action_t *action, bool (*cancelled)(void *),
                    void (*emit)(void *, const openlogi_action_t *), void *context);
#endif
