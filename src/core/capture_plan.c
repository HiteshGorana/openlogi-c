/**
 * @file capture_plan.c
 * @brief Implementation of capture plan resolution and diversion spec.
 */

#include "capture_plan.h"

#include <string.h>

void capture_spec_init(capture_spec_t *spec)
{
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
}

bool capture_spec_add_cid(capture_spec_t *spec, uint16_t cid)
{
    if (!spec || spec->divert_count >= MAX_DIVERT_CIDS) return false;

    for (size_t i = 0; i < spec->divert_count; i++) {
        if (spec->divert_cids[i] == cid) {
            return true; /* Already included */
        }
    }

    spec->divert_cids[spec->divert_count++] = cid;
    return true;
}

bool capture_spec_has_cid(const capture_spec_t *spec, uint16_t cid)
{
    if (!spec) return false;
    for (size_t i = 0; i < spec->divert_count; i++) {
        if (spec->divert_cids[i] == cid) return true;
    }
    return false;
}

static void add_profile_cids(const config_app_profile_t *prof, capture_spec_t *out_spec)
{
    if (!prof) return;
    for (size_t i = 0; i < prof->binding_count; i++) {
        const config_button_binding_t *b = &prof->bindings[i];
        if (b->button_id == CONFIG_BUTTON_BACK) {
            capture_spec_add_cid(out_spec, CID_BACK_BUTTON);
        } else if (b->button_id == CONFIG_BUTTON_FORWARD) {
            capture_spec_add_cid(out_spec, CID_FORWARD_BUTTON);
        } else if (b->button_id == CONFIG_BUTTON_MIDDLE) {
            capture_spec_add_cid(out_spec, CID_MIDDLE_BUTTON);
        } else if (b->button_id == CONFIG_BUTTON_GESTURE || b->is_gesture) {
            capture_spec_add_cid(out_spec, CID_GESTURE_BUTTON);
            out_spec->wants_raw_xy = true;
        }
    }
}

void capture_plan_resolve(const config_device_t *dev, capture_spec_t *out_spec)
{
    if (!out_spec) return;
    capture_spec_init(out_spec);
    if (!dev) return;

    /* Global default profile */
    add_profile_cids(&dev->default_profile, out_spec);

    /* Per-app profiles */
    for (size_t a = 0; a < dev->app_profile_count; a++) {
        add_profile_cids(&dev->app_profiles[a], out_spec);
    }
}

bool capture_spec_needs_rearm(const capture_spec_t *old_spec, const capture_spec_t *new_spec)
{
    if (!old_spec || !new_spec) return true;
    if (old_spec->wants_raw_xy != new_spec->wants_raw_xy) return true;
    if (old_spec->divert_count != new_spec->divert_count) return true;

    for (size_t i = 0; i < old_spec->divert_count; i++) {
        if (!capture_spec_has_cid(new_spec, old_spec->divert_cids[i])) {
            return true;
        }
    }
    return false;
}
