/**
 * @file test_config.c
 * @brief Unit tests for configuration parser, profile resolver, and watchers.
 */

#include "config.h"
#include "config_watch.h"
#include "app_watch.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define TEST_PASS(name) (void)(name)

TEST_SUITE_DECLARE(g_suite);

static const char *k_test_toml =
    "schema_version = 7\n"
    "selected_device = \"receiver:aabbccdd:slot:1\"\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\"]\n"
    "custom_name = \"Office mouse\"\n"
    "dpi = 1600\n"
    "dpi_presets = [800, 1600, 3200]\n"
    "invert_scroll = true\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\".smartshift]\n"
    "mode = \"ratchet\"\n"
    "auto_disengage = 16\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\".bindings]\n"
    "Back = \"BrowserBack\"\n"
    "Forward = \"BrowserForward\"\n"
    "MiddleClick = { HoldShortcut = \"Ctrl+Space\" }\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\".bindings.GestureButton]\n"
    "Click = \"MissionControl\"\n"
    "Up = \"MissionControl\"\n"
    "Down = \"AppExpose\"\n"
    "Left = \"PreviousDesktop\"\n"
    "Right = \"NextDesktop\"\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\".per_app_bindings.\"com.microsoft.VSCode\"]\n"
    "Back = \"Undo\"\n"
    "\n"
    "[devices.\"receiver:aabbccdd:slot:1\".per_app_bindings.\"com.apple.Safari\"]\n"
    "MiddleClick = { CustomShortcut = \"Cmd+Shift+T\" }\n";

static void test_config_parse_full_example(void)
{
    openlogi_config_t cfg;
    char errbuf[256] = {0};

    bool ok = config_parse_string(k_test_toml, &cfg, errbuf, sizeof(errbuf));
    if (!ok) {
        fprintf(stderr, "Parse error: %s\n", errbuf);
    }
    assert(ok && "config_parse_string must succeed");

    assert(cfg.schema_version == 7);
    assert(strcmp(cfg.selected_device, "receiver:aabbccdd:slot:1") == 0);
    assert(cfg.device_count == 1);

    const config_device_t *dev = &cfg.devices[0];
    assert(strcmp(dev->device_key, "receiver:aabbccdd:slot:1") == 0);
    assert(strcmp(dev->custom_name, "Office mouse") == 0);
    assert(dev->dpi == 1600);
    assert(dev->invert_scroll == true);
    assert(dev->dpi_preset_count == 3);
    assert(dev->dpi_presets[0] == 800);
    assert(dev->dpi_presets[1] == 1600);
    assert(dev->dpi_presets[2] == 3200);

    /* SmartShift */
    assert(dev->smartshift_mode == 1); /* ratchet */
    assert(dev->smartshift_threshold == 16);

    /* Default bindings */
    assert(dev->default_profile.binding_count >= 3);

    /* Per-app profiles */
    assert(dev->app_profile_count == 2);
    assert(strcmp(dev->app_profiles[0].app_id, "com.microsoft.VSCode") == 0);
    assert(strcmp(dev->app_profiles[1].app_id, "com.apple.Safari") == 0);

    config_free(&cfg);
    TEST_PASS("test_config_parse_full_example");
}

static void test_config_action_mappings(void)
{
    openlogi_action_t act;

    assert(action_from_string("MissionControl", &act));
    assert(act.type == ACTION_MISSION_CONTROL);

    assert(action_from_string("AppExpose", &act));
    assert(act.type == ACTION_APP_EXPOSE);

    assert(action_from_string("ShowDesktop", &act));
    assert(act.type == ACTION_SHOW_DESKTOP);

    assert(action_from_string("BrowserBack", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert((act.u.key_combo.modifiers & MOD_CMD) != 0);

    assert(action_from_string("BrowserForward", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert((act.u.key_combo.modifiers & MOD_CMD) != 0);

    assert(action_from_string("PlayPause", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_PLAY_PAUSE);

    assert(action_from_string("Mute", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_MUTE);

    TEST_PASS("test_config_action_mappings");
}

static void test_config_profile_resolution(void)
{
    openlogi_config_t cfg;
    bool ok = config_parse_string(k_test_toml, &cfg, NULL, 0);
    assert(ok);

    const char *dev_key = "receiver:aabbccdd:slot:1";

    /* Case A: In default context (no foreground app / desktop) */
    const config_button_binding_t *b_back = config_resolve_binding(&cfg, dev_key, NULL, CONFIG_BUTTON_BACK);
    assert(b_back != NULL);
    assert(b_back->action.type == ACTION_KEY_COMBO); /* BrowserBack */

    const config_button_binding_t *b_fwd = config_resolve_binding(&cfg, dev_key, "com.apple.Finder", CONFIG_BUTTON_FORWARD);
    assert(b_fwd != NULL);
    assert(b_fwd->action.type == ACTION_KEY_COMBO); /* BrowserForward */

    const config_button_binding_t *b_gesture = config_resolve_binding(&cfg, dev_key, NULL, CONFIG_BUTTON_GESTURE);
    assert(b_gesture != NULL);
    assert(b_gesture->is_gesture == true);
    assert(b_gesture->gesture_up.type == ACTION_MISSION_CONTROL);
    assert(b_gesture->gesture_down.type == ACTION_APP_EXPOSE);
    assert(b_gesture->gesture_left.type == ACTION_PREV_SPACE);
    assert(b_gesture->gesture_right.type == ACTION_NEXT_SPACE);
    assert(b_gesture->action.type == ACTION_MISSION_CONTROL);

    /* Case B: In VSCode context */
    /* Back button should be overridden to Undo */
    const config_button_binding_t *b_vscode_back = config_resolve_binding(&cfg, dev_key, "com.microsoft.VSCode", CONFIG_BUTTON_BACK);
    assert(b_vscode_back != NULL);
    assert(b_vscode_back->action.type == ACTION_KEY_COMBO);
    assert(b_vscode_back->action.u.key_combo.keycode == 0x06); /* 'z' for Undo */
    assert((b_vscode_back->action.u.key_combo.modifiers & MOD_CMD) != 0);

    /* Forward button is not overridden in VSCode -> falls back to BrowserForward */
    const config_button_binding_t *b_vscode_fwd = config_resolve_binding(&cfg, dev_key, "com.microsoft.VSCode", CONFIG_BUTTON_FORWARD);
    assert(b_vscode_fwd != NULL);
    assert(b_vscode_fwd->action.type == ACTION_KEY_COMBO);

    /* Gesture button is not overridden in VSCode -> falls back to default gesture */
    const config_button_binding_t *b_vscode_gesture = config_resolve_binding(&cfg, dev_key, "com.microsoft.VSCode", CONFIG_BUTTON_GESTURE);
    assert(b_vscode_gesture != NULL);
    assert(b_vscode_gesture->is_gesture == true);
    assert(b_vscode_gesture->gesture_up.type == ACTION_MISSION_CONTROL);

    /* Case C: In Safari context */
    /* Middle click should be overridden to Cmd+Shift+T */
    const config_button_binding_t *b_safari_mid = config_resolve_binding(&cfg, dev_key, "com.apple.Safari", CONFIG_BUTTON_MIDDLE);
    assert(b_safari_mid != NULL);
    assert(b_safari_mid->action.type == ACTION_KEY_COMBO);
    assert((b_safari_mid->action.u.key_combo.modifiers & MOD_CMD) != 0);
    assert((b_safari_mid->action.u.key_combo.modifiers & MOD_SHIFT) != 0);

    TEST_PASS("test_config_profile_resolution");
}

static pthread_mutex_t g_callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_callback_ready = PTHREAD_COND_INITIALIZER;
static bool g_watch_reloaded = false;
static char g_reloaded_path[512] = {0};

static bool wait_for_callback(bool *notified)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;

    pthread_mutex_lock(&g_callback_lock);
    int result = 0;
    while (!*notified && result != ETIMEDOUT) {
        result = pthread_cond_timedwait(&g_callback_ready, &g_callback_lock, &deadline);
        assert(result == 0 || result == ETIMEDOUT);
    }
    bool observed = *notified;
    pthread_mutex_unlock(&g_callback_lock);
    return observed;
}

static void on_test_config_reload(const char *path, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&g_callback_lock);
    if (path) {
        snprintf(g_reloaded_path, sizeof(g_reloaded_path), "%s", path);
    }
    g_watch_reloaded = true;
    pthread_cond_signal(&g_callback_ready);
    pthread_mutex_unlock(&g_callback_lock);
}

static void test_config_file_watcher(void)
{
    const char *test_path = "/tmp/openlogi_test_watch.toml";

    /* Create initial file */
    FILE *fp = fopen(test_path, "w");
    assert(fp != NULL);
    fputs("schema_version = 7\n", fp);
    fclose(fp);

    pthread_mutex_lock(&g_callback_lock);
    g_watch_reloaded = false;
    memset(g_reloaded_path, 0, sizeof(g_reloaded_path));
    pthread_mutex_unlock(&g_callback_lock);

    /* Start watcher */
    bool started = config_watch_start(test_path, on_test_config_reload, NULL);
    assert(started && "config_watch_start should succeed");
    assert(config_watch_is_running());

    /* Wait a moment for kqueue thread to arm */
    usleep(50000);

    /* Modify file */
    fp = fopen(test_path, "a");
    assert(fp != NULL);
    fputs("selected_device = \"test:123\"\n", fp);
    fclose(fp);

    /* The notification and path are published together under the callback lock. */
    assert(wait_for_callback(&g_watch_reloaded) && "Watcher should trigger on file change");
    pthread_mutex_lock(&g_callback_lock);
    assert(strcmp(g_reloaded_path, test_path) == 0);
    pthread_mutex_unlock(&g_callback_lock);

    /* Stop watcher */
    config_watch_stop();
    assert(!config_watch_is_running());

    unlink(test_path);
    TEST_PASS("test_config_file_watcher");
}

static bool g_app_cb_fired = false;
static void test_app_cb(const char *id, void *ud) {
    (void)id; (void)ud;
    pthread_mutex_lock(&g_callback_lock);
    g_app_cb_fired = true;
    pthread_cond_signal(&g_callback_ready);
    pthread_mutex_unlock(&g_callback_lock);
}

static void test_app_watcher(void)
{
    char app_id[256] = {0};
    bool ok = app_watch_get_frontmost(app_id, sizeof(app_id));
    if (ok) {
        printf("    [i] Frontmost app currently active: %s\n", app_id);
    }
    assert(ok && "app_watch_get_frontmost should succeed on macOS");
    assert(strlen(app_id) > 0);

    /* Test start and stop */
    bool started = app_watch_start(NULL, NULL);
    assert(!started && "app_watch_start with NULL callback should fail");

    /* Start with real callback */
    pthread_mutex_lock(&g_callback_lock);
    g_app_cb_fired = false;
    pthread_mutex_unlock(&g_callback_lock);
    started = app_watch_start(test_app_cb, NULL);
    assert(started);
    assert(app_watch_is_running());

    /* App watcher fires immediately with current app */
    assert(wait_for_callback(&g_app_cb_fired) && "app_watch callback should fire with initial app");

    /* Test tick execution */
    app_watch_tick();

    app_watch_stop();
    assert(!app_watch_is_running());

    TEST_PASS("test_app_watcher");
}

static void test_config_free_workflows(void)
{
    const char *wf_toml =
        "schema_version = 7\n"
        "[devices.default]\n"
        "[devices.default.bindings]\n"
        "MiddleClick = { Workflow = [\n"
        "    { PressKey = \"Cmd+Space\" },\n"
        "    { Delay = { millis = 150 } },\n"
        "    { TypeText = \"Terminal\" }\n"
        "] }\n";

    openlogi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    char errbuf[256] = {0};
    bool ok = config_parse_string(wf_toml, &cfg, errbuf, sizeof(errbuf));
    assert(ok && "Parsing workflow TOML should succeed");
    assert(cfg.device_count == 1);
    assert(cfg.devices[0].default_profile.binding_count == 1);
    assert(cfg.devices[0].default_profile.bindings[0].action.type == ACTION_WORKFLOW);
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow != NULL);
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow->step_count == 3);
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow->steps[1].type == WORKFLOW_STEP_DELAY);
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow->steps[1].u.delay_ms == 150);

    /* Free config memory */
    config_free(&cfg);
    assert(cfg.device_count == 0);

    /* Also verify documented integer delay syntax: { Delay = 250 } */
    const char *wf_toml2 =
        "schema_version = 7\n"
        "[devices.default]\n"
        "[devices.default.bindings]\n"
        "MiddleClick = { Workflow = [\n"
        "    { PressKey = \"Cmd+Space\" },\n"
        "    { Delay = 250 },\n"
        "    { TypeText = \"Terminal\" }\n"
        "] }\n";
    ok = config_parse_string(wf_toml2, &cfg, errbuf, sizeof(errbuf));
    assert(ok && "Parsing integer Delay should succeed");
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow->steps[1].type == WORKFLOW_STEP_DELAY);
    assert(cfg.devices[0].default_profile.bindings[0].action.u.workflow->steps[1].u.delay_ms == 250);
    config_free(&cfg);

    /* Verify malformed workflow step rejection */
    const char *bad_wf_toml =
        "schema_version = 7\n"
        "[devices.default]\n"
        "[devices.default.bindings]\n"
        "MiddleClick = { Workflow = [\n"
        "    { UnknownStep = \"foobar\" }\n"
        "] }\n";
    ok = config_parse_string(bad_wf_toml, &cfg, errbuf, sizeof(errbuf));
    assert(!ok && "Unknown workflow step should be rejected");

    TEST_PASS("test_config_free_workflows");
}

static void test_config_transactional_reload_on_error(void)
{
    openlogi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    char errbuf[256] = {0};

    /* First, load valid configuration */
    bool ok = config_parse_string(k_test_toml, &cfg, errbuf, sizeof(errbuf));
    assert(ok);
    assert(cfg.device_count == 1);
    assert(cfg.devices[0].dpi == 1600);

    /* Attempt parsing syntax-broken TOML into existing cfg */
    const char *bad_toml = "schema_version = 7\n[devices.default\nthis is invalid syntax !!!";
    ok = config_parse_string(bad_toml, &cfg, errbuf, sizeof(errbuf));
    assert(!ok && "Syntax error TOML should fail");
    assert(strlen(errbuf) > 0);

    /* Transactional check: existing config MUST NOT be wiped to 0 */
    assert(cfg.schema_version == 7);
    assert(cfg.device_count == 1);
    assert(cfg.devices[0].dpi == 1600);
    assert(strcmp(cfg.selected_device, "receiver:aabbccdd:slot:1") == 0);

    config_free(&cfg);
    TEST_PASS("test_config_transactional_reload_on_error");
}

int main(void)
{
    test_suite_begin(&g_suite, "test_config", "TOML Schema Parser, Profile Resolution & Watchers");

    TEST_CASE_RUN(g_suite, test_config_parse_full_example);
    TEST_CASE_RUN(g_suite, test_config_action_mappings);
    TEST_CASE_RUN(g_suite, test_config_profile_resolution);
    TEST_CASE_RUN(g_suite, test_config_free_workflows);
    TEST_CASE_RUN(g_suite, test_config_transactional_reload_on_error);
    TEST_CASE_RUN(g_suite, test_config_file_watcher);
    TEST_CASE_RUN(g_suite, test_app_watcher);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
