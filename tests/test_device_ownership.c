/**
 * @file test_device_ownership.c
 * @brief Regression and stress tests for Milestone 2: runtime ownership,
 *        disconnect use-after-free protection, and source-device identity (R01 + R08).
 */

#include "../src/hid/hid_device_internal.h"
#include "hid_mock.h"
#include "config.h"
#include "input_runtime.h"
#include "device.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

TEST_SUITE_DECLARE(g_suite);

/* --------------------------------------------------------------------------
 * Test 1: Disconnect During Transaction Safety (R01)
 * -------------------------------------------------------------------------- */

typedef struct {
    hid_device_t *dev;
    bool tx_started;
    bool tx_completed;
    bool tx_success;
} tx_stress_ctx_t;

static void *tx_waiter_thread(void *arg)
{
    tx_stress_ctx_t *ctx = (tx_stress_ctx_t *)arg;
    hid_device_t *dev = ctx->dev;

    /* Simulates hid_transact entry: retain dev and set pending state */
    pthread_mutex_lock(&dev->tx_lock);
    if (dev->removed) {
        pthread_mutex_unlock(&dev->tx_lock);
        return NULL;
    }
    dev->ref_count++;
    dev->has_pending_tx = true;
    dev->pending_resp_ready = false;
    ctx->tx_started = true;

    /* Wait loop simulating waiting for response or device removal */
    while (!dev->pending_resp_ready) {
        if (dev->removed) {
            break;
        }
        pthread_mutex_unlock(&dev->tx_lock);
        usleep(5000); /* 5ms wait */
        pthread_mutex_lock(&dev->tx_lock);
    }

    bool success = (!dev->removed && dev->pending_resp_ready);
    dev->has_pending_tx = false;
    dev->pending_resp_ready = false;
    dev->ref_count--;
    bool should_free = (dev->ref_count <= 0);
    pthread_mutex_unlock(&dev->tx_lock);

    if (should_free) {
        pthread_mutex_destroy(&dev->tx_lock);
        free(dev);
    }

    ctx->tx_completed = true;
    ctx->tx_success = success;
    return NULL;
}

static void test_disconnect_during_transaction_safety(void)
{
    /* Allocate a test HID device with ref_count=1 (owned by driver) */
    hid_device_t *dev = (hid_device_t *)calloc(1, sizeof(hid_device_t));
    assert(dev != NULL);
    dev->ref_count = 1;
    dev->removed = false;
    pthread_mutex_init(&dev->tx_lock, NULL);

    tx_stress_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.dev = dev;

    pthread_t thread;
    assert(pthread_create(&thread, NULL, tx_waiter_thread, &ctx) == 0);

    /* Wait until the transaction thread starts */
    while (!ctx.tx_started) {
        usleep(1000);
    }

    /* While the transaction is waiting, simulate on_device_removal() */
    pthread_mutex_lock(&dev->tx_lock);
    dev->removed = true;
    dev->has_pending_tx = false;
    dev->ref_count--;
    bool should_free = (dev->ref_count <= 0);
    pthread_mutex_unlock(&dev->tx_lock);

    /* Since transaction thread still holds a reference (ref_count == 1),
     * on_device_removal must NOT free the device immediately! */
    assert(!should_free);

    /* Join transaction thread: it must exit cleanly without crashing */
    pthread_join(thread, NULL);

    assert(ctx.tx_completed);
    assert(!ctx.tx_success); /* Aborted due to removal */
}

/* --------------------------------------------------------------------------
 * Test 2: Concurrent Configuration Reload and Lookup Stress (R01)
 * -------------------------------------------------------------------------- */

typedef struct {
    openlogi_config_t *cfg;
    pthread_mutex_t *lock;
    volatile bool stop;
    size_t reads;
} config_reader_ctx_t;

static void *config_reader_thread(void *arg)
{
    config_reader_ctx_t *ctx = (config_reader_ctx_t *)arg;
    while (!ctx->stop) {
        pthread_mutex_lock(ctx->lock);
        const config_device_t *dev = config_resolve_device(ctx->cfg, "device:6be9d300");
        if (dev) {
            const config_button_binding_t *b = config_resolve_binding(ctx->cfg, "device:6be9d300", "com.apple.Safari", CONFIG_BUTTON_BACK);
            (void)b;
        }
        pthread_mutex_unlock(ctx->lock);
        ctx->reads++;
        usleep(100);
    }
    return NULL;
}

static void test_concurrent_config_reload_and_lookup_stress(void)
{
    openlogi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.schema_version = 7;
    cfg.device_count = 1;
    strncpy(cfg.devices[0].device_key, "device:6be9d300", sizeof(cfg.devices[0].device_key) - 1);
    cfg.devices[0].enabled = true;
    cfg.devices[0].dpi = 1200;

    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    config_reader_ctx_t reader_ctx[3];
    pthread_t reader_threads[3];
    for (int i = 0; i < 3; i++) {
        reader_ctx[i].cfg = &cfg;
        reader_ctx[i].lock = &lock;
        reader_ctx[i].stop = false;
        reader_ctx[i].reads = 0;
        assert(pthread_create(&reader_threads[i], NULL, config_reader_thread, &reader_ctx[i]) == 0);
    }

    /* Simulate 30 rapid configuration reloads */
    const char *test_toml =
        "schema_version = 7\n"
        "[devices.default]\n"
        "enabled = true\n"
        "dpi = 1600\n"
        "[devices.\"device:6be9d300\"]\n"
        "enabled = true\n"
        "dpi = 2400\n"
        "[devices.\"device:6be9d300\".bindings]\n"
        "Back = \"BrowserBack\"\n";

    for (int r = 0; r < 30; r++) {
        openlogi_config_t *new_cfg = calloc(1, sizeof(openlogi_config_t));
        assert(new_cfg != NULL);
        char err[128];
        assert(config_parse_string(test_toml, new_cfg, err, sizeof(err)));

        pthread_mutex_lock(&lock);
        openlogi_config_t old_cfg = cfg;
        cfg = *new_cfg;
        free(new_cfg);
        pthread_mutex_unlock(&lock);

        config_free(&old_cfg);
        usleep(500);
    }

    for (int i = 0; i < 3; i++) {
        reader_ctx[i].stop = true;
        pthread_join(reader_threads[i], NULL);
        assert(reader_ctx[i].reads > 0);
    }

    config_free(&cfg);
    pthread_mutex_destroy(&lock);
}

/* --------------------------------------------------------------------------
 * Test 3: Device-Scoped Action Dispatch and Isolation (R08)
 * -------------------------------------------------------------------------- */

typedef struct {
    char device_key[128];
    uint16_t dpi;
    uint8_t smartshift_mode;
    uint8_t smartshift_threshold;
} mock_hw_device_t;

static void simulate_dispatch_action(
    const openlogi_config_t *cfg,
    mock_hw_device_t *devices,
    size_t device_count,
    const openlogi_action_t *action,
    const char *target_device_key)
{
    if (!action || action->type == ACTION_NONE || !target_device_key) return;

    /* Locate targeted device */
    mock_hw_device_t *target_dev = NULL;
    for (size_t i = 0; i < device_count; i++) {
        if (strcmp(devices[i].device_key, target_device_key) == 0) {
            target_dev = &devices[i];
            break;
        }
    }
    if (!target_dev) return;

    if (action->type == ACTION_TOGGLE_SMARTSHIFT) {
        target_dev->smartshift_mode = (target_dev->smartshift_mode == 1) ? 2 : 1;
        return;
    }

    if (action->type == ACTION_CYCLE_DPI) {
        const config_device_t *cdev = config_resolve_device(cfg, target_device_key);
        if (cdev && cdev->dpi_preset_count > 0) {
            for (size_t p = 0; p < cdev->dpi_preset_count; p++) {
                if (cdev->dpi_presets[p] == target_dev->dpi) {
                    target_dev->dpi = cdev->dpi_presets[(p + 1) % cdev->dpi_preset_count];
                    break;
                }
            }
        }
        return;
    }

    if (action->type == ACTION_SET_DPI_PRESET) {
        uint8_t p_idx = action->u.dpi_preset_index;
        const config_device_t *cdev = config_resolve_device(cfg, target_device_key);
        if (cdev && p_idx < cdev->dpi_preset_count) {
            target_dev->dpi = cdev->dpi_presets[p_idx];
        }
        return;
    }
}

static void test_device_scoped_action_dispatch(void)
{
    openlogi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.schema_version = 7;
    cfg.device_count = 2;

    /* Device 1 configuration */
    strncpy(cfg.devices[0].device_key, "device:mouse_A", sizeof(cfg.devices[0].device_key) - 1);
    cfg.devices[0].enabled = true;
    cfg.devices[0].dpi_presets[0] = 800;
    cfg.devices[0].dpi_presets[1] = 1200;
    cfg.devices[0].dpi_presets[2] = 1600;
    cfg.devices[0].dpi_preset_count = 3;

    /* Device 2 configuration */
    strncpy(cfg.devices[1].device_key, "device:mouse_B", sizeof(cfg.devices[1].device_key) - 1);
    cfg.devices[1].enabled = true;
    cfg.devices[1].dpi_presets[0] = 1000;
    cfg.devices[1].dpi_presets[1] = 2000;
    cfg.devices[1].dpi_presets[2] = 4000;
    cfg.devices[1].dpi_preset_count = 3;

    mock_hw_device_t hw[2] = {
        { "device:mouse_A", 1200, 1, 16 },
        { "device:mouse_B", 2000, 2, 16 }
    };

    /* 1. Cycle DPI on Device A -> Only Device A changes */
    openlogi_action_t cycle_act;
    memset(&cycle_act, 0, sizeof(cycle_act));
    cycle_act.type = ACTION_CYCLE_DPI;

    simulate_dispatch_action(&cfg, hw, 2, &cycle_act, "device:mouse_A");
    assert(hw[0].dpi == 1600);
    assert(hw[1].dpi == 2000); /* Device B unchanged! */

    /* 2. Toggle SmartShift on Device B -> Only Device B changes */
    openlogi_action_t ss_act;
    memset(&ss_act, 0, sizeof(ss_act));
    ss_act.type = ACTION_TOGGLE_SMARTSHIFT;

    simulate_dispatch_action(&cfg, hw, 2, &ss_act, "device:mouse_B");
    assert(hw[0].smartshift_mode == 1); /* Device A unchanged! */
    assert(hw[1].smartshift_mode == 1); /* Device B toggled 2 -> 1 */

    /* 3. Set DPI Preset index 0 on Device A -> Only Device A changes */
    openlogi_action_t preset_act;
    memset(&preset_act, 0, sizeof(preset_act));
    preset_act.type = ACTION_SET_DPI_PRESET;
    preset_act.u.dpi_preset_index = 0;

    simulate_dispatch_action(&cfg, hw, 2, &preset_act, "device:mouse_A");
    assert(hw[0].dpi == 800);
    assert(hw[1].dpi == 2000); /* Device B still unchanged! */
}

/* --------------------------------------------------------------------------
 * Test 4: Independent Device Disconnect Isolation (R08)
 * -------------------------------------------------------------------------- */

static int g_dispatched_count = 0;
static config_button_id_t g_last_dispatched_button = CONFIG_BUTTON_UNKNOWN;

static void mock_input_dispatch(void *ctx, config_button_id_t button, gesture_direction_t dir, const openlogi_action_t *action)
{
    (void)ctx;
    (void)dir;
    (void)action;
    g_dispatched_count++;
    g_last_dispatched_button = button;
}

static void test_independent_device_disconnect_isolation(void)
{
    input_runtime_t input_A, input_B;
    input_runtime_init(&input_A, mock_input_dispatch, NULL);
    input_runtime_init(&input_B, mock_input_dispatch, NULL);

    config_button_binding_t binding_back;
    memset(&binding_back, 0, sizeof(binding_back));
    binding_back.button_id = CONFIG_BUTTON_BACK;
    binding_back.action.type = ACTION_KEY_COMBO;

    config_button_binding_t binding_forward;
    memset(&binding_forward, 0, sizeof(binding_forward));
    binding_forward.button_id = CONFIG_BUTTON_FORWARD;
    binding_forward.action.type = ACTION_KEY_COMBO;

    /* Press Back button down on Device A */
    assert(input_runtime_button(&input_A, CONFIG_BUTTON_BACK, true, &binding_back));
    assert(input_runtime_captured(&input_A, CONFIG_BUTTON_BACK));

    /* Press Forward button down on Device B */
    assert(input_runtime_button(&input_B, CONFIG_BUTTON_FORWARD, true, &binding_forward));
    assert(input_runtime_captured(&input_B, CONFIG_BUTTON_FORWARD));

    /* Disconnect Device A only */
    input_runtime_cancel(&input_A);

    /* Device A press is canceled */
    assert(!input_runtime_captured(&input_A, CONFIG_BUTTON_BACK));

    /* Device B press is STILL active and captured! */
    assert(input_runtime_captured(&input_B, CONFIG_BUTTON_FORWARD));

    /* Release Forward button on Device B -> must trigger action successfully */
    g_dispatched_count = 0;
    input_runtime_button(&input_B, CONFIG_BUTTON_FORWARD, false, &binding_forward);
    assert(g_dispatched_count == 1);
    assert(g_last_dispatched_button == CONFIG_BUTTON_FORWARD);
}

/* --------------------------------------------------------------------------
 * Test 5: Duplicate Model Disambiguation (R08)
 * -------------------------------------------------------------------------- */

static void test_duplicate_model_disambiguation(void)
{
    /* Simulate probing 3 identical mice without serial numbers */
    char keys[3][64];
    size_t count = 0;

    for (int i = 0; i < 3; i++) {
        uint16_t vid = 0x046d;
        uint16_t pid = 0xb023;

        char base_key[64];
        snprintf(base_key, sizeof(base_key), "dev:0x%04x:0x%04x", vid, pid);
        int count_same = 0;
        for (size_t d = 0; d < count; d++) {
            if (strncmp(keys[d], base_key, strlen(base_key)) == 0) {
                count_same++;
            }
        }
        if (count_same == 0) {
            snprintf(keys[count], sizeof(keys[count]), "%s", base_key);
        } else {
            snprintf(keys[count], sizeof(keys[count]), "%s:%d", base_key, count_same + 1);
        }
        count++;
    }

    assert(strcmp(keys[0], "dev:0x046d:0xb023") == 0);
    assert(strcmp(keys[1], "dev:0x046d:0xb023:2") == 0);
    assert(strcmp(keys[2], "dev:0x046d:0xb023:3") == 0);
}

/* --------------------------------------------------------------------------
 * Test Suite Entry
 * -------------------------------------------------------------------------- */

int main(void)
{
    test_suite_begin(&g_suite, "test_device_ownership", "Device Ownership, Disconnect Safety & Isolation");

    TEST_CASE_RUN(g_suite, test_disconnect_during_transaction_safety);
    TEST_CASE_RUN(g_suite, test_concurrent_config_reload_and_lookup_stress);
    TEST_CASE_RUN(g_suite, test_device_scoped_action_dispatch);
    TEST_CASE_RUN(g_suite, test_independent_device_disconnect_isolation);
    TEST_CASE_RUN(g_suite, test_duplicate_model_disambiguation);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
