#ifndef OPENLOGI_DAEMON_SAFETY_H
#define OPENLOGI_DAEMON_SAFETY_H
#include <stdbool.h>
#include <stddef.h>

/* Called by the runtime owner. Producers stop before cancellation/restoration;
 * waiting for child jobs is deliberately last. Adapters are borrowed. */
typedef struct {
    bool (*start_hook)(void *);
    void (*stop_hook)(void *);
    void (*start_watch)(void *);
    void (*stop_watch)(void *);
    void (*cancel_input)(void *);
    bool (*apply_capture)(void *);
    void (*restore_input)(void *);
    void (*stop_jobs)(void *);
    void (*cancel_jobs)(void *);
} daemon_safety_ops_t;
bool daemon_safety_reconcile(size_t usable_devices, bool permissions, bool hook_running,
                             const daemon_safety_ops_t *ops, void *context);
void daemon_safety_stop(const daemon_safety_ops_t *ops, void *context);
#endif
