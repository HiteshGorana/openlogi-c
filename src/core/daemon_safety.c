#include "daemon_safety.h"

static void deactivate(const daemon_safety_ops_t *ops, void *context)
{
    ops->stop_hook(context);
    ops->stop_watch(context);
    ops->cancel_input(context);
    ops->restore_input(context);
}

bool daemon_safety_reconcile(size_t devices, bool permissions, bool hook_running,
                             const daemon_safety_ops_t *ops, void *context)
{
    /* Reconcile real subsystems even if the previous state was already standby. */
    if (!devices || !permissions || (!hook_running && !ops->start_hook(context))) {
        deactivate(ops, context);
        return false;
    }
    ops->start_watch(context);
    if (!ops->apply_capture(context)) {
        deactivate(ops, context);
        return false;
    }
    return true;
}

void daemon_safety_stop(const daemon_safety_ops_t *ops, void *context)
{
    ops->cancel_jobs(context);
    deactivate(ops, context);
    ops->stop_jobs(context);
}
