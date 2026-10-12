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

static volatile bool g_watch_reloaded = false;
static char g_reloaded_path[512] = {0};

static void on_test_config_reload(const char *path, void *user_data)
{
    (void)user_data;
    g_watch_reloaded = true;
    if (path) {
        strncpy(g_reloaded_path, path, sizeof(g_reloaded_path) - 1);
    }
}

static void test_config_file_watcher(void)
{
    const char *test_path = "/tmp/openlogi_test_watch.toml";

    /* Create initial file */
    FILE *fp = fopen(test_path, "w");
    assert(fp != NULL);
    fputs("schema_version = 7\n", fp);
    fclose(fp);

    g_watch_reloaded = false;
    memset(g_reloaded_path, 0, sizeof(g_reloaded_path));

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

    /* Wait for notification */
    for (int i = 0; i < 20; i++) {
        if (g_watch_reloaded) break;
        usleep(50000); /* 50ms */
    }

    assert(g_watch_reloaded && "Watcher should trigger on file change");
    assert(strcmp(g_reloaded_path, test_path) == 0);

    /* Stop watcher */
    config_watch_stop();
    assert(!config_watch_is_running());

    unlink(test_path);
    TEST_PASS("test_config_file_watcher");
}

static volatile bool g_app_cb_fired = false;
static void test_app_cb(const char *id, void *ud) {
    (void)id; (void)ud;
    g_app_cb_fired = true;
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
    g_app_cb_fired = false;
    started = app_watch_start(test_app_cb, NULL);
    assert(started);
    assert(app_watch_is_running());

    /* App watcher fires immediately with current app */
    usleep(150000);
    assert(g_app_cb_fired && "app_watch callback should fire with initial app");

    app_watch_stop();
    assert(!app_watch_is_running());

    TEST_PASS("test_app_watcher");
}

int main(void)
{
    test_suite_begin(&g_suite, "test_config", "TOML Schema Parser, Profile Resolution & Watchers");

    TEST_CASE_RUN(g_suite, test_config_parse_full_example);
    TEST_CASE_RUN(g_suite, test_config_action_mappings);
    TEST_CASE_RUN(g_suite, test_config_profile_resolution);
    TEST_CASE_RUN(g_suite, test_config_file_watcher);
    TEST_CASE_RUN(g_suite, test_app_watcher);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
