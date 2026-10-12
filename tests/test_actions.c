/**
 * @file test_actions.c
 * @brief Comprehensive verification for all 53 actions defined in OpenLogi ACTIONS.md.
 */

#include "config.h"
#include "inject.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

TEST_SUITE_DECLARE(g_suite);

static void test_mouse_actions(void)
{
    openlogi_action_t act;

    /* 1. LeftClick */
    assert(action_from_string("LeftClick", &act));
    assert(act.type == ACTION_MOUSE_CLICK);
    assert(act.u.mouse_click.button == 1);

    /* 2. RightClick */
    assert(action_from_string("RightClick", &act));
    assert(act.type == ACTION_MOUSE_CLICK);
    assert(act.u.mouse_click.button == 2);

    /* 3. MiddleClick */
    assert(action_from_string("MiddleClick", &act));
    assert(act.type == ACTION_MOUSE_CLICK);
    assert(act.u.mouse_click.button == 3);

    /* 4. MouseBack (Button 4) */
    assert(action_from_string("MouseBack", &act));
    assert(act.type == ACTION_MOUSE_CLICK);
    assert(act.u.mouse_click.button == 4);

    /* 5. MouseForward (Button 5) */
    assert(action_from_string("MouseForward", &act));
    assert(act.type == ACTION_MOUSE_CLICK);
    assert(act.u.mouse_click.button == 5);
}

static void test_editing_actions(void)
{
    openlogi_action_t act;

    /* 6. Copy (Cmd+C: 0x08) */
    assert(action_from_string("Copy", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x08);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 7. Paste (Cmd+V: 0x09) */
    assert(action_from_string("Paste", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x09);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 8. Cut (Cmd+X: 0x07) */
    assert(action_from_string("Cut", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x07);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 9. Undo (Cmd+Z: 0x06) */
    assert(action_from_string("Undo", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x06);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 10. Redo (Cmd+Shift+Z: 0x06) */
    assert(action_from_string("Redo", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x06);
    assert(act.u.key_combo.modifiers == (MOD_CMD | MOD_SHIFT));

    /* 11. SelectAll (Cmd+A: 0x00) */
    assert(action_from_string("SelectAll", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x00);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 12. Find (Cmd+F: 0x03) */
    assert(action_from_string("Find", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x03);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 13. Save (Cmd+S: 0x01) */
    assert(action_from_string("Save", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x01);
    assert(act.u.key_combo.modifiers == MOD_CMD);
}

static void test_browser_actions(void)
{
    openlogi_action_t act;

    /* 14. BrowserBack (Cmd+[: 0x21) */
    assert(action_from_string("BrowserBack", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x21);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 15. BrowserForward (Cmd+]: 0x1E) */
    assert(action_from_string("BrowserForward", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x1E);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 16. NewTab (Cmd+T: 0x11) */
    assert(action_from_string("NewTab", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x11);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 17. CloseTab (Cmd+W: 0x0D) */
    assert(action_from_string("CloseTab", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x0D);
    assert(act.u.key_combo.modifiers == MOD_CMD);

    /* 18. ReopenTab (Cmd+Shift+T: 0x11) */
    assert(action_from_string("ReopenTab", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x11);
    assert(act.u.key_combo.modifiers == (MOD_CMD | MOD_SHIFT));

    /* 19. NextTab (Ctrl+Tab: 0x30) */
    assert(action_from_string("NextTab", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x30);
    assert(act.u.key_combo.modifiers == MOD_CTRL);

    /* 20. PrevTab (Ctrl+Shift+Tab: 0x30) */
    assert(action_from_string("PrevTab", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x30);
    assert(act.u.key_combo.modifiers == (MOD_CTRL | MOD_SHIFT));

    /* 21. ReloadPage (Cmd+R: 0x0F) */
    assert(action_from_string("ReloadPage", &act));
    assert(act.type == ACTION_KEY_COMBO);
    assert(act.u.key_combo.keycode == 0x0F);
    assert(act.u.key_combo.modifiers == MOD_CMD);
}

static void test_media_and_scroll_actions(void)
{
    openlogi_action_t act;

    /* 22. PlayPause */
    assert(action_from_string("PlayPause", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_PLAY_PAUSE);

    /* 23. NextTrack */
    assert(action_from_string("NextTrack", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_NEXT);

    /* 24. PrevTrack */
    assert(action_from_string("PrevTrack", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_PREVIOUS);

    /* 25. VolumeUp */
    assert(action_from_string("VolumeUp", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_VOLUME_UP);

    /* 26. VolumeDown */
    assert(action_from_string("VolumeDown", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_VOLUME_DOWN);

    /* 27. MuteVolume / Mute */
    assert(action_from_string("MuteVolume", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_MUTE);

    assert(action_from_string("Mute", &act));
    assert(act.type == ACTION_MEDIA_KEY);
    assert(act.u.media_key == MEDIA_KEY_MUTE);

    /* 28. ScrollUp */
    assert(action_from_string("ScrollUp", &act));
    assert(act.type == ACTION_SCROLL);
    assert(act.u.scroll.dy > 0);

    /* 29. ScrollDown */
    assert(action_from_string("ScrollDown", &act));
    assert(act.type == ACTION_SCROLL);
    assert(act.u.scroll.dy < 0);

    /* 30. HorizontalScrollLeft */
    assert(action_from_string("HorizontalScrollLeft", &act));
    assert(act.type == ACTION_SCROLL);
    assert(act.u.scroll.dx < 0);

    /* 31. HorizontalScrollRight */
    assert(action_from_string("HorizontalScrollRight", &act));
    assert(act.type == ACTION_SCROLL);
    assert(act.u.scroll.dx > 0);
}

static void test_system_and_device_actions(void)
{
    openlogi_action_t act;

    /* 32. None */
    assert(action_from_string("None", &act));
    assert(act.type == ACTION_NONE);

    /* 33. MissionControl */
    assert(action_from_string("MissionControl", &act));
    assert(act.type == ACTION_MISSION_CONTROL);

    /* 34. AppExpose */
    assert(action_from_string("AppExpose", &act));
    assert(act.type == ACTION_APP_EXPOSE);

    /* 35. PreviousDesktop / PreviousSpace */
    assert(action_from_string("PreviousDesktop", &act));
    assert(act.type == ACTION_PREV_SPACE);
    assert(action_from_string("PreviousSpace", &act));
    assert(act.type == ACTION_PREV_SPACE);

    /* 36. NextDesktop / NextSpace */
    assert(action_from_string("NextDesktop", &act));
    assert(act.type == ACTION_NEXT_SPACE);
    assert(action_from_string("NextSpace", &act));
    assert(act.type == ACTION_NEXT_SPACE);

    /* 37. ShowDesktop */
    assert(action_from_string("ShowDesktop", &act));
    assert(act.type == ACTION_SHOW_DESKTOP);

    /* 38. LaunchpadShow / Launchpad */
    assert(action_from_string("LaunchpadShow", &act));
    assert(act.type == ACTION_LAUNCHPAD);
    assert(action_from_string("Launchpad", &act));
    assert(act.type == ACTION_LAUNCHPAD);

    /* 39. LockScreen */
    assert(action_from_string("LockScreen", &act));
    assert(act.type == ACTION_LOCK_SCREEN);

    /* 40. Screenshot */
    assert(action_from_string("Screenshot", &act));
    assert(act.type == ACTION_SCREENSHOT);

    /* 41. CaptureRegion */
    assert(action_from_string("CaptureRegion", &act));
    assert(act.type == ACTION_CAPTURE_REGION);

    /* 42. Sleep */
    assert(action_from_string("Sleep", &act));
    assert(act.type == ACTION_SLEEP);

    /* 43. ShowActionsRing */
    assert(action_from_string("ShowActionsRing", &act));
    assert(act.type == ACTION_SHOW_ACTIONS_RING);

    /* 44. CycleDpiPresets */
    assert(action_from_string("CycleDpiPresets", &act));
    assert(act.type == ACTION_CYCLE_DPI);

    /* 45. ToggleSmartShift */
    assert(action_from_string("ToggleSmartShift", &act));
    assert(act.type == ACTION_TOGGLE_SMARTSHIFT);
}

static void test_parameterized_and_workflow_actions(void)
{
    /* 46-53: Parameterized TOML actions */
    const char *toml_sample =
        "schema_version = 7\n"
        "[devices.\"mouse:1\".bindings]\n"
        "Back = { CustomShortcut = \"Cmd+Shift+P\" }\n"
        "Forward = { HoldShortcut = \"Ctrl+Space\" }\n"
        "MiddleClick = { SetDpiPreset = 2 }\n"
        "DpiToggle = { OpenApplication = \"/System/Applications/Calculator.app\" }\n"
        "WheelTiltLeft = { TypeText = \"Hello Pure C\" }\n"
        "WheelTiltRight = { RunAppleScript = \"display notification \\\"Hi\\\"\" }\n"
        "GestureButton = { RunShellCommand = \"echo test > /dev/null\" }\n"
        "ThumbwheelScrollUp = { Workflow = [\n"
        "    { PressKey = \"Cmd+C\" },\n"
        "    { Delay = { millis = 100 } },\n"
        "    { TypeText = \"Pasted: \" },\n"
        "    { PressKey = \"Cmd+V\" }\n"
        "] }\n";

    openlogi_config_t cfg;
    char errbuf[256] = {0};
    bool ok = config_parse_string(toml_sample, &cfg, errbuf, sizeof(errbuf));
    if (!ok) {
        fprintf(stderr, "Error parsing parameterized actions: %s\n", errbuf);
    }
    assert(ok);
    assert(cfg.device_count == 1);

    const config_device_t *dev = &cfg.devices[0];
    assert(dev->default_profile.binding_count == 8);

    /* 46. CustomShortcut */
    const config_button_binding_t *b_back = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_BACK);
    assert(b_back != NULL);
    assert(b_back->action.type == ACTION_KEY_COMBO);
    assert(b_back->action.u.key_combo.modifiers == (MOD_CMD | MOD_SHIFT));
    assert(b_back->action.u.key_combo.hold == false);

    /* 47. HoldShortcut */
    const config_button_binding_t *b_fwd = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_FORWARD);
    assert(b_fwd != NULL);
    assert(b_fwd->action.type == ACTION_KEY_COMBO);
    assert(b_fwd->action.u.key_combo.modifiers == MOD_CTRL);
    assert(b_fwd->action.u.key_combo.keycode == 0x31); /* Space */
    assert(b_fwd->action.u.key_combo.hold == true);

    /* 48. SetDpiPreset */
    const config_button_binding_t *b_mid = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_MIDDLE);
    assert(b_mid != NULL);
    assert(b_mid->action.type == ACTION_SET_DPI_PRESET);
    assert(b_mid->action.u.dpi_preset_index == 2);

    /* 49. OpenApplication */
    const config_button_binding_t *b_dpi = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_DPI_TOGGLE);
    assert(b_dpi != NULL);
    assert(b_dpi->action.type == ACTION_APP_OPEN);
    assert(strcmp(b_dpi->action.u.app_path, "/System/Applications/Calculator.app") == 0);

    /* 50. TypeText */
    const config_button_binding_t *b_tilt_l = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_WHEEL_TILT_LEFT);
    assert(b_tilt_l != NULL);
    assert(b_tilt_l->action.type == ACTION_TEXT);
    assert(strcmp(b_tilt_l->action.u.text, "Hello Pure C") == 0);

    /* 51. RunAppleScript */
    const config_button_binding_t *b_tilt_r = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_WHEEL_TILT_RIGHT);
    assert(b_tilt_r != NULL);
    assert(b_tilt_r->action.type == ACTION_APPLE_SCRIPT);
    assert(strcmp(b_tilt_r->action.u.apple_script, "display notification \"Hi\"") == 0);

    /* 52. RunShellCommand */
    const config_button_binding_t *b_gest = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_GESTURE);
    assert(b_gest != NULL);
    assert(b_gest->action.type == ACTION_SHELL_COMMAND);
    assert(strcmp(b_gest->action.u.shell_cmd, "echo test > /dev/null") == 0);

    /* 53. Workflow */
    const config_button_binding_t *b_wf = config_resolve_binding(&cfg, "mouse:1", NULL, CONFIG_BUTTON_THUMBWHEEL_UP);
    assert(b_wf != NULL);
    assert(b_wf->action.type == ACTION_WORKFLOW);
    assert(b_wf->action.u.workflow != NULL);
    assert(b_wf->action.u.workflow->step_count == 4);
    assert(b_wf->action.u.workflow->steps[0].type == WORKFLOW_STEP_PRESS_KEY);
    assert(b_wf->action.u.workflow->steps[0].u.key.keycode == 0x08); /* 'c' */
    assert(b_wf->action.u.workflow->steps[1].type == WORKFLOW_STEP_DELAY);
    assert(b_wf->action.u.workflow->steps[1].u.delay_ms == 100);
    assert(b_wf->action.u.workflow->steps[2].type == WORKFLOW_STEP_TYPE_TEXT);
    assert(strcmp(b_wf->action.u.workflow->steps[2].u.text, "Pasted: ") == 0);
    assert(b_wf->action.u.workflow->steps[3].type == WORKFLOW_STEP_PRESS_KEY);
    assert(b_wf->action.u.workflow->steps[3].u.key.keycode == 0x09); /* 'v' */

    /* Clean up heap workflow */
    free(b_wf->action.u.workflow);
}

int main(void)
{
    test_suite_begin(&g_suite, "test_actions", "Complete 53-Action Catalog & Workflow Engine Verification");

    TEST_CASE_RUN(g_suite, test_mouse_actions);
    TEST_CASE_RUN(g_suite, test_editing_actions);
    TEST_CASE_RUN(g_suite, test_browser_actions);
    TEST_CASE_RUN(g_suite, test_media_and_scroll_actions);
    TEST_CASE_RUN(g_suite, test_system_and_device_actions);
    TEST_CASE_RUN(g_suite, test_parameterized_and_workflow_actions);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
