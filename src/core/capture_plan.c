/**
 * @file capture_plan.c
 * @brief Implementation of capture plan resolution and diversion spec.
 */

#include "capture_plan.h"
#include "input_runtime.h"
#include "hidpp_reprog.h"

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

static void add_binding(const config_button_binding_t *binding, capture_spec_t *out)
{
    if (!input_binding_usable(binding)) return;
    uint16_t cid = 0;
    switch (binding->button_id) {
        case CONFIG_BUTTON_BACK: cid = CID_BACK_BUTTON; break;
        case CONFIG_BUTTON_FORWARD: cid = CID_FORWARD_BUTTON; break;
        case CONFIG_BUTTON_MIDDLE: cid = CID_MIDDLE_BUTTON; break;
        case CONFIG_BUTTON_GESTURE: cid = CID_GESTURE_BUTTON; break;
        case CONFIG_BUTTON_DPI_TOGGLE: cid = HIDPP_CID_MODE_SHIFT_SMARTSHIFT; break;
        default: return;
    }
    capture_spec_add_cid(out, cid);
    if (binding->is_gesture) out->wants_raw_xy = true;
}

void capture_plan_resolve_for_app(const config_device_t *dev, const char *app, capture_spec_t *out)
{
    capture_spec_init(out);
    if (!dev || !dev->enabled) return;
    const config_app_profile_t *override = NULL;
    for (size_t i = 0; app && i < dev->app_profile_count; i++)
        if (strcmp(dev->app_profiles[i].app_id, app) == 0) override = &dev->app_profiles[i];
    for (int button = CONFIG_BUTTON_MIDDLE; button <= CONFIG_BUTTON_GESTURE; button++) {
        const config_button_binding_t *binding = NULL;
        for (size_t i = 0; i < dev->default_profile.binding_count; i++)
            if (dev->default_profile.bindings[i].button_id == (config_button_id_t)button) binding = &dev->default_profile.bindings[i];
        for (size_t i = 0; override && i < override->binding_count; i++)
            if (override->bindings[i].button_id == (config_button_id_t)button) binding = &override->bindings[i];
        add_binding(binding, out);
    }
}

void capture_plan_resolve(const config_device_t *dev, capture_spec_t *out)
{
    capture_plan_resolve_for_app(dev, NULL, out);
}

static void remove_cid(capture_spec_t *spec, size_t index)
{
    memmove(&spec->divert_cids[index], &spec->divert_cids[index + 1],
            (spec->divert_count - index - 1) * sizeof(spec->divert_cids[0]));
    spec->divert_count--;
}

bool capture_plan_apply(capture_spec_t *applied, const capture_spec_t *desired,
                        capture_write_fn write, void *context)
{
    if (!applied || !desired || !write) return false;
    bool ok = true;
    for (size_t i = 0; i < applied->divert_count;) {
        if (!capture_spec_has_cid(desired, applied->divert_cids[i]) ||
            applied->wants_raw_xy != desired->wants_raw_xy) {
            if (write(context, applied->divert_cids[i], false, false)) remove_cid(applied, i);
            else { ok = false; i++; }
        } else i++;
    }
    if (!ok) return false; /* Never add captures while restoration is unconfirmed. */
    size_t kept = applied->divert_count;
    for (size_t i = 0; i < desired->divert_count; i++) {
        uint16_t cid = desired->divert_cids[i];
        if (capture_spec_has_cid(applied, cid)) continue;
        if (applied->divert_count == MAX_DIVERT_CIDS || !write(context, cid, true, desired->wants_raw_xy)) {
            for (size_t j = applied->divert_count; j > kept; j--)
                if (write(context, applied->divert_cids[j - 1], false, false)) remove_cid(applied, j - 1);
            return false;
        }
        capture_spec_add_cid(applied, cid);
        applied->wants_raw_xy = desired->wants_raw_xy;
    }
    applied->wants_raw_xy = desired->wants_raw_xy;
    return true;
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
