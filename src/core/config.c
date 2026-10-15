/**
 * @file config.c
 * @brief Implementation of TOML configuration parser, validator, and profile resolver.
 */

#include "config.h"
#include "toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper for trim / lowercase */
static void str_tolower(char *dst, const char *src, size_t max_len)
{
    if (!dst || !src || max_len == 0) return;
    size_t i = 0;
    for (; src[i] && i + 1 < max_len; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

bool config_get_default_path(char *out_path, size_t max_len)
{
    if (!out_path || max_len == 0) return false;

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        snprintf(out_path, max_len, "%s/openlogi/config.toml", xdg);
        return true;
    }

    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(out_path, max_len, "%s/.config/openlogi/config.toml", home);
        return true;
    }

    return false;
}

config_button_id_t button_id_from_string(const char *str)
{
    if (!str) return CONFIG_BUTTON_UNKNOWN;
    char lower[64];
    str_tolower(lower, str, sizeof(lower));

    if (strcmp(lower, "back") == 0) return CONFIG_BUTTON_BACK;
    if (strcmp(lower, "forward") == 0) return CONFIG_BUTTON_FORWARD;
    if (strcmp(lower, "middle") == 0 || strcmp(lower, "middleclick") == 0) return CONFIG_BUTTON_MIDDLE;
    if (strcmp(lower, "dpitoggle") == 0 || strcmp(lower, "modeshift") == 0) return CONFIG_BUTTON_DPI_TOGGLE;
    if (strcmp(lower, "gesture") == 0 || strcmp(lower, "gesturebutton") == 0) return CONFIG_BUTTON_GESTURE;
    if (strcmp(lower, "thumbwheel") == 0) return CONFIG_BUTTON_THUMBWHEEL;
    if (strcmp(lower, "wheeltiltleft") == 0) return CONFIG_BUTTON_WHEEL_TILT_LEFT;
    if (strcmp(lower, "wheeltiltright") == 0) return CONFIG_BUTTON_WHEEL_TILT_RIGHT;
    if (strcmp(lower, "thumbwheelscrollup") == 0) return CONFIG_BUTTON_THUMBWHEEL_UP;
    if (strcmp(lower, "thumbwheelscrolldown") == 0) return CONFIG_BUTTON_THUMBWHEEL_DOWN;
    if (strcmp(lower, "hapticpanel") == 0) return CONFIG_BUTTON_HAPTIC_PANEL;
    if (strcmp(lower, "left") == 0 || strcmp(lower, "leftclick") == 0) return CONFIG_BUTTON_LEFT;
    if (strcmp(lower, "right") == 0 || strcmp(lower, "rightclick") == 0) return CONFIG_BUTTON_RIGHT;

    return CONFIG_BUTTON_UNKNOWN;
}

const char *button_id_to_string(config_button_id_t btn)
{
    switch (btn) {
        case CONFIG_BUTTON_LEFT: return "LeftClick";
        case CONFIG_BUTTON_RIGHT: return "RightClick";
        case CONFIG_BUTTON_MIDDLE: return "MiddleClick";
        case CONFIG_BUTTON_BACK: return "Back";
        case CONFIG_BUTTON_FORWARD: return "Forward";
        case CONFIG_BUTTON_DPI_TOGGLE: return "DpiToggle";
        case CONFIG_BUTTON_GESTURE: return "GestureButton";
        case CONFIG_BUTTON_THUMBWHEEL: return "ThumbWheel";
        case CONFIG_BUTTON_WHEEL_TILT_LEFT: return "WheelTiltLeft";
        case CONFIG_BUTTON_WHEEL_TILT_RIGHT: return "WheelTiltRight";
        case CONFIG_BUTTON_THUMBWHEEL_UP: return "ThumbWheelScrollUp";
        case CONFIG_BUTTON_THUMBWHEEL_DOWN: return "ThumbWheelScrollDown";
        case CONFIG_BUTTON_HAPTIC_PANEL: return "HapticPanel";
        default: return "Unknown";
    }
}

static uint16_t macos_char_to_keycode(char c)
{
    c = (char)tolower((unsigned char)c);
    switch (c) {
        case 'a': return 0x00;
        case 's': return 0x01;
        case 'd': return 0x02;
        case 'f': return 0x03;
        case 'h': return 0x04;
        case 'g': return 0x05;
        case 'z': return 0x06;
        case 'x': return 0x07;
        case 'c': return 0x08;
        case 'v': return 0x09;
        case 'b': return 0x0B;
        case 'q': return 0x0C;
        case 'w': return 0x0D;
        case 'e': return 0x0E;
        case 'r': return 0x0F;
        case 'y': return 0x10;
        case 't': return 0x11;
        case '1': return 0x12;
        case '2': return 0x13;
        case '3': return 0x14;
        case '4': return 0x15;
        case '6': return 0x16;
        case '5': return 0x17;
        case '=': return 0x18;
        case '9': return 0x19;
        case '7': return 0x1A;
        case '-': return 0x1B;
        case '8': return 0x1C;
        case '0': return 0x1D;
        case ']': return 0x1E;
        case 'o': return 0x1F;
        case 'u': return 0x20;
        case '[': return 0x21;
        case 'i': return 0x22;
        case 'p': return 0x23;
        case 'l': return 0x25;
        case 'j': return 0x26;
        case '\'': return 0x27;
        case 'k': return 0x28;
        case ';': return 0x29;
        case '\\': return 0x2A;
        case ',': return 0x2B;
        case '/': return 0x2C;
        case 'n': return 0x2D;
        case 'm': return 0x2E;
        case '.': return 0x2F;
        case '`': return 0x32;
        case ' ': return 0x31;
        default:  return 0xFFFF;
    }
}

static uint16_t macos_name_to_keycode(const char *name)
{
    if (!name || name[0] == '\0') return 0xFFFF;
    char lower[32];
    str_tolower(lower, name, sizeof(lower));

    if (strcmp(lower, "space") == 0) return 0x31;
    if (strcmp(lower, "tab") == 0) return 0x30;
    if (strcmp(lower, "return") == 0 || strcmp(lower, "enter") == 0) return 0x24;
    if (strcmp(lower, "esc") == 0 || strcmp(lower, "escape") == 0) return 0x35;
    if (strcmp(lower, "backspace") == 0 || strcmp(lower, "delete") == 0) return 0x33;
    if (strcmp(lower, "forwarddelete") == 0) return 0x75;
    if (strcmp(lower, "left") == 0) return 0x7B;
    if (strcmp(lower, "right") == 0) return 0x7C;
    if (strcmp(lower, "down") == 0) return 0x7D;
    if (strcmp(lower, "up") == 0) return 0x7E;
    if (strcmp(lower, "home") == 0) return 0x73;
    if (strcmp(lower, "end") == 0) return 0x77;
    if (strcmp(lower, "pageup") == 0 || strcmp(lower, "page-up") == 0) return 0x74;
    if (strcmp(lower, "pagedown") == 0 || strcmp(lower, "page-down") == 0) return 0x79;

    /* Function keys F1 - F20 */
    if (lower[0] == 'f' && isdigit((unsigned char)lower[1])) {
        char *end = NULL;
        long fnum = strtol(lower + 1, &end, 10);
        if (!end || *end) return 0xFFFF;
        switch (fnum) {
            case 1: return 0x7A;
            case 2: return 0x78;
            case 3: return 0x63;
            case 4: return 0x76;
            case 5: return 0x60;
            case 6: return 0x61;
            case 7: return 0x62;
            case 8: return 0x64;
            case 9: return 0x65;
            case 10: return 0x6D;
            case 11: return 0x67;
            case 12: return 0x6F;
            case 13: return 0x69;
            case 14: return 0x6B;
            case 15: return 0x71;
            case 16: return 0x6A;
            case 17: return 0x40;
            case 18: return 0x4F;
            case 19: return 0x50;
            case 20: return 0x5A;
            default: break;
        }
    }

    if (strcmp(lower, "minus") == 0) return 0x1B;
    if (strcmp(lower, "equals") == 0) return 0x18;
    if (strcmp(lower, "semicolon") == 0) return 0x29;
    if (strcmp(lower, "apostrophe") == 0) return 0x27;
    if (strcmp(lower, "backtick") == 0) return 0x32;
    if (strcmp(lower, "comma") == 0) return 0x2B;
    if (strcmp(lower, "period") == 0) return 0x2F;
    if (strcmp(lower, "slash") == 0) return 0x2C;
    if (strcmp(lower, "backslash") == 0) return 0x2A;

    if (strlen(name) == 1) {
        return macos_char_to_keycode(name[0]);
    }

    return 0xFFFF;
}

static bool parse_shortcut(const char *str, openlogi_action_t *out_action)
{
    if (!str || str[0] == '\0' || !out_action) return false;

    uint32_t modifiers = 0;
    uint16_t keycode = 0xFFFF;
    char buf[128];
    if (strlen(str) >= sizeof(buf) || str[0] == '+' || str[strlen(str) - 1] == '+' || strstr(str, "++")) return false;
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(buf, "+", &saveptr);
    while (token) {
        /* Trim leading and trailing spaces */
        while (*token == ' ') token++;
        size_t len = strlen(token);
        while (len > 0 && token[len - 1] == ' ') token[--len] = '\0';

        char lower[32];
        str_tolower(lower, token, sizeof(lower));

        if (strcmp(lower, "cmd") == 0 || strcmp(lower, "command") == 0 ||
            strcmp(lower, "win") == 0 || strcmp(lower, "super") == 0 || strcmp(lower, "meta") == 0) {
            modifiers |= MOD_CMD;
        } else if (strcmp(lower, "ctrl") == 0 || strcmp(lower, "control") == 0) {
            modifiers |= MOD_CTRL;
        } else if (strcmp(lower, "alt") == 0 || strcmp(lower, "option") == 0 || strcmp(lower, "opt") == 0) {
            modifiers |= MOD_ALT;
        } else if (strcmp(lower, "shift") == 0) {
            modifiers |= MOD_SHIFT;
        } else {
            uint16_t kc = macos_name_to_keycode(token);
            if (kc == 0xFFFF || keycode != 0xFFFF) return false;
            keycode = kc;
        }
        token = strtok_r(NULL, "+", &saveptr);
    }

    if (keycode != 0xFFFF) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = keycode;
        out_action->u.key_combo.modifiers = modifiers;
        return true;
    }
    return false;
}

bool action_from_string(const char *str, openlogi_action_t *out_action)
{
    if (!str || !out_action) return false;
    memset(out_action, 0, sizeof(*out_action));

    char lower[128];
    str_tolower(lower, str, sizeof(lower));

    /* 1. System / Suppress */
    if (strcmp(lower, "none") == 0) {
        out_action->type = ACTION_NONE;
        return true;
    }

    /* 2. Mouse Clicks */
    if (strcmp(lower, "leftclick") == 0) {
        out_action->type = ACTION_MOUSE_CLICK;
        out_action->u.mouse_click.button = 1;
        return true;
    }
    if (strcmp(lower, "rightclick") == 0) {
        out_action->type = ACTION_MOUSE_CLICK;
        out_action->u.mouse_click.button = 2;
        return true;
    }
    if (strcmp(lower, "middleclick") == 0) {
        out_action->type = ACTION_MOUSE_CLICK;
        out_action->u.mouse_click.button = 3;
        return true;
    }
    if (strcmp(lower, "mouseback") == 0) {
        out_action->type = ACTION_MOUSE_CLICK;
        out_action->u.mouse_click.button = 4;
        return true;
    }
    if (strcmp(lower, "mouseforward") == 0) {
        out_action->type = ACTION_MOUSE_CLICK;
        out_action->u.mouse_click.button = 5;
        return true;
    }

    /* 3. Editing */
    if (strcmp(lower, "copy") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x08; /* 'c' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "paste") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x09; /* 'v' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "cut") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x07; /* 'x' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "undo") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x06; /* 'z' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "redo") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x06; /* 'z' */
        out_action->u.key_combo.modifiers = MOD_CMD | MOD_SHIFT;
        return true;
    }
    if (strcmp(lower, "selectall") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x00; /* 'a' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "find") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x03; /* 'f' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "save") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x01; /* 's' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }

    /* 4. Browser Navigation */
    if (strcmp(lower, "browserback") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x21; /* '[' key */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "browserforward") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x1E; /* ']' key */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "newtab") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x11; /* 't' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "closetab") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x0D; /* 'w' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }
    if (strcmp(lower, "reopentab") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x11; /* 't' */
        out_action->u.key_combo.modifiers = MOD_CMD | MOD_SHIFT;
        return true;
    }
    if (strcmp(lower, "nexttab") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x30; /* Tab */
        out_action->u.key_combo.modifiers = MOD_CTRL;
        return true;
    }
    if (strcmp(lower, "prevtab") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x30; /* Tab */
        out_action->u.key_combo.modifiers = MOD_CTRL | MOD_SHIFT;
        return true;
    }
    if (strcmp(lower, "reloadpage") == 0) {
        out_action->type = ACTION_KEY_COMBO;
        out_action->u.key_combo.keycode = 0x0F; /* 'r' */
        out_action->u.key_combo.modifiers = MOD_CMD;
        return true;
    }

    /* 5. Media & Volume */
    if (strcmp(lower, "playpause") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_PLAY_PAUSE;
        return true;
    }
    if (strcmp(lower, "nexttrack") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_NEXT;
        return true;
    }
    if (strcmp(lower, "prevtrack") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_PREVIOUS;
        return true;
    }
    if (strcmp(lower, "volumeup") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_VOLUME_UP;
        return true;
    }
    if (strcmp(lower, "volumedown") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_VOLUME_DOWN;
        return true;
    }
    if (strcmp(lower, "mutevolume") == 0 || strcmp(lower, "mute") == 0) {
        out_action->type = ACTION_MEDIA_KEY;
        out_action->u.media_key = MEDIA_KEY_MUTE;
        return true;
    }

    /* 6. Scrolling */
    if (strcmp(lower, "scrollup") == 0) {
        out_action->type = ACTION_SCROLL;
        out_action->u.scroll.dx = 0.0;
        out_action->u.scroll.dy = 5.0;
        return true;
    }
    if (strcmp(lower, "scrolldown") == 0) {
        out_action->type = ACTION_SCROLL;
        out_action->u.scroll.dx = 0.0;
        out_action->u.scroll.dy = -5.0;
        return true;
    }
    if (strcmp(lower, "horizontalscrollleft") == 0) {
        out_action->type = ACTION_SCROLL;
        out_action->u.scroll.dx = -5.0;
        out_action->u.scroll.dy = 0.0;
        return true;
    }
    if (strcmp(lower, "horizontalscrollright") == 0) {
        out_action->type = ACTION_SCROLL;
        out_action->u.scroll.dx = 5.0;
        out_action->u.scroll.dy = 0.0;
        return true;
    }

    /* 7. Navigation & System */
    if (strcmp(lower, "missioncontrol") == 0) {
        out_action->type = ACTION_MISSION_CONTROL;
        return true;
    }
    if (strcmp(lower, "appexpose") == 0) {
        out_action->type = ACTION_APP_EXPOSE;
        return true;
    }
    if (strcmp(lower, "previousdesktop") == 0 || strcmp(lower, "previousspace") == 0) {
        out_action->type = ACTION_PREV_SPACE;
        return true;
    }
    if (strcmp(lower, "nextdesktop") == 0 || strcmp(lower, "nextspace") == 0) {
        out_action->type = ACTION_NEXT_SPACE;
        return true;
    }
    if (strcmp(lower, "showdesktop") == 0) {
        out_action->type = ACTION_SHOW_DESKTOP;
        return true;
    }
    if (strcmp(lower, "launchpadshow") == 0 || strcmp(lower, "launchpad") == 0) {
        out_action->type = ACTION_LAUNCHPAD;
        return true;
    }
    if (strcmp(lower, "lockscreen") == 0) {
        out_action->type = ACTION_LOCK_SCREEN;
        return true;
    }
    if (strcmp(lower, "screenshot") == 0) {
        out_action->type = ACTION_SCREENSHOT;
        return true;
    }
    if (strcmp(lower, "captureregion") == 0) {
        out_action->type = ACTION_CAPTURE_REGION;
        return true;
    }
    if (strcmp(lower, "sleep") == 0) {
        out_action->type = ACTION_SLEEP;
        return true;
    }
    if (strcmp(lower, "showactionsring") == 0) {
        out_action->type = ACTION_SHOW_ACTIONS_RING;
        return true;
    }

    /* 8. Device Actions */
    if (strcmp(lower, "cycledpipresets") == 0) {
        out_action->type = ACTION_CYCLE_DPI;
        return true;
    }
    if (strcmp(lower, "togglesmartshift") == 0) {
        out_action->type = ACTION_TOGGLE_SMARTSHIFT;
        return true;
    }

    /* Check if it's a key combo with '+' */
    if (strchr(str, '+') != NULL) {
        return parse_shortcut(str, out_action);
    }

    return false;
}

/* Parse into a private workflow, publishing only a fully validated sequence. */
static bool parse_workflow(const toml_array_t *array, openlogi_action_t *action,
                           const char *button, char *error, size_t error_size)
{
    int count = array ? toml_array_nelem(array) : 0;
    if (count < 1 || count > MAX_WORKFLOW_STEPS) {
        snprintf(error, error_size, "%s.Workflow: expected 1..%d steps", button, MAX_WORKFLOW_STEPS);
        return false;
    }
    openlogi_workflow_t *workflow = calloc(1, sizeof(*workflow));
    if (!workflow) {
        snprintf(error, error_size, "%s.Workflow: allocation failed", button);
        return false;
    }
    for (int i = 0; i < count; i++) {
        const toml_table_t *table = toml_table_at(array, i);
        const char *key = table ? toml_key_in(table, 0) : NULL;
        workflow_step_t *step = &workflow->steps[i];
        if (!key || toml_key_in(table, 1)) goto invalid;
        if (strcmp(key, "Delay") == 0) {
            toml_datum_t ms = toml_int_in(table, key);
            const toml_table_t *legacy = toml_table_in(table, key);
            if (legacy) {
                const char *field = toml_key_in(legacy, 0);
                if (!field || strcmp(field, "millis") || toml_key_in(legacy, 1)) goto invalid;
                ms = toml_int_in(legacy, "millis");
            }
            if (!ms.ok || ms.u.i < 0 || ms.u.i > MAX_WORKFLOW_DELAY_MS) goto invalid;
            step->type = WORKFLOW_STEP_DELAY;
            step->u.delay_ms = (uint32_t)ms.u.i;
        } else {
            toml_datum_t value = toml_string_in(table, key);
            if (!value.ok) goto invalid;
            bool valid = false;
            if (strcmp(key, "PressKey") == 0) {
                openlogi_action_t shortcut = { 0 };
                valid = parse_shortcut(value.u.s, &shortcut);
                step->type = WORKFLOW_STEP_PRESS_KEY;
                step->u.key.keycode = shortcut.u.key_combo.keycode;
                step->u.key.modifiers = shortcut.u.key_combo.modifiers;
            } else {
                char *out = NULL;
                size_t capacity = 0;
                if (strcmp(key, "TypeText") == 0) {
                    step->type = WORKFLOW_STEP_TYPE_TEXT;
                    out = step->u.text; capacity = sizeof(step->u.text);
                } else if (strcmp(key, "RunAppleScript") == 0) {
                    step->type = WORKFLOW_STEP_APPLE_SCRIPT;
                    out = step->u.script; capacity = sizeof(step->u.script);
                } else if (strcmp(key, "RunShellCommand") == 0) {
                    step->type = WORKFLOW_STEP_SHELL_CMD;
                    out = step->u.cmd; capacity = sizeof(step->u.cmd);
                }
                valid = out && strlen(value.u.s) < capacity;
                if (valid) memcpy(out, value.u.s, strlen(value.u.s) + 1);
            }
            free(value.u.s);
            if (!valid) goto invalid;
        }
        continue;
invalid:
        snprintf(error, error_size, "%s.Workflow[%d]: invalid, ambiguous, or oversized step", button, i + 1);
        free(workflow);
        return false;
    }
    workflow->step_count = (size_t)count;
    action->type = ACTION_WORKFLOW;
    action->u.workflow = workflow;
    return true;
}

static bool parse_button_bindings_table(
    const toml_table_t *bindings_tab,
    config_button_binding_t *out_bindings,
    size_t *out_count,
    size_t max_count, char *error, size_t error_size)
{
    if (!bindings_tab || !out_bindings || !out_count) return false;

    for (int i = 0; *out_count < max_count; i++) {
        const char *key_name = toml_key_in(bindings_tab, i);
        if (!key_name) break;

        config_button_id_t btn_id = button_id_from_string(key_name);
        if (btn_id == CONFIG_BUTTON_UNKNOWN) continue;

        config_button_binding_t *binding = &out_bindings[*out_count];
        memset(binding, 0, sizeof(*binding));
        binding->button_id = btn_id;

        /* Case 1: Simple string action */
        toml_datum_t str_val = toml_string_in(bindings_tab, key_name);
        if (str_val.ok) {
            action_from_string(str_val.u.s, &binding->action);
            free(str_val.u.s);
            (*out_count)++;
            continue;
        }

        /* Case 2: Table value (Gesture directions or Parameterized action table) */
        const toml_table_t *sub_tab = toml_table_in(bindings_tab, key_name);
        if (sub_tab) {
            /* Workflow is exclusive; don't let an earlier key hide malformed steps. */
            bool has_workflow = false;
            for (int k = 0; toml_key_in(sub_tab, k); k++)
                if (strcmp(toml_key_in(sub_tab, k), "Workflow") == 0) has_workflow = true;
            if (has_workflow) {
                if (toml_key_in(sub_tab, 1)) {
                    snprintf(error, error_size, "%s.Workflow: ambiguous action object", key_name);
                    return false;
                }
                if (!parse_workflow(toml_array_in(sub_tab, "Workflow"), &binding->action,
                                    key_name, error, error_size)) return false;
                (*out_count)++;
                continue;
            }
            /* 1. CustomShortcut */
            toml_datum_t cs = toml_string_in(sub_tab, "CustomShortcut");
            if (cs.ok) {
                parse_shortcut(cs.u.s, &binding->action);
                binding->action.u.key_combo.hold = false;
                free(cs.u.s);
                (*out_count)++;
                continue;
            }

            /* 2. HoldShortcut */
            toml_datum_t hs = toml_string_in(sub_tab, "HoldShortcut");
            if (hs.ok) {
                parse_shortcut(hs.u.s, &binding->action);
                binding->action.u.key_combo.hold = true;
                free(hs.u.s);
                (*out_count)++;
                continue;
            }

            /* 3. SetDpiPreset */
            toml_datum_t dpi_p = toml_int_in(sub_tab, "SetDpiPreset");
            if (dpi_p.ok) {
                binding->action.type = ACTION_SET_DPI_PRESET;
                binding->action.u.dpi_preset_index = (uint8_t)dpi_p.u.i;
                (*out_count)++;
                continue;
            }

            /* 4. OpenApplication */
            toml_datum_t app_s = toml_string_in(sub_tab, "OpenApplication");
            if (app_s.ok) {
                binding->action.type = ACTION_APP_OPEN;
                strncpy(binding->action.u.app_path, app_s.u.s, sizeof(binding->action.u.app_path) - 1);
                free(app_s.u.s);
                (*out_count)++;
                continue;
            } else {
                const toml_table_t *app_tab = toml_table_in(sub_tab, "OpenApplication");
                if (app_tab) {
                    toml_datum_t p = toml_string_in(app_tab, "path");
                    if (p.ok) {
                        binding->action.type = ACTION_APP_OPEN;
                        strncpy(binding->action.u.app_path, p.u.s, sizeof(binding->action.u.app_path) - 1);
                        free(p.u.s);
                        (*out_count)++;
                        continue;
                    }
                }
            }

            /* 5. TypeText */
            toml_datum_t tt = toml_string_in(sub_tab, "TypeText");
            if (tt.ok) {
                binding->action.type = ACTION_TEXT;
                strncpy(binding->action.u.text, tt.u.s, sizeof(binding->action.u.text) - 1);
                free(tt.u.s);
                (*out_count)++;
                continue;
            }

            /* 6. RunAppleScript */
            toml_datum_t as_val = toml_string_in(sub_tab, "RunAppleScript");
            if (as_val.ok) {
                binding->action.type = ACTION_APPLE_SCRIPT;
                strncpy(binding->action.u.apple_script, as_val.u.s, sizeof(binding->action.u.apple_script) - 1);
                free(as_val.u.s);
                (*out_count)++;
                continue;
            }

            /* 7. RunShellCommand */
            toml_datum_t sc_val = toml_string_in(sub_tab, "RunShellCommand");
            if (sc_val.ok) {
                binding->action.type = ACTION_SHELL_COMMAND;
                strncpy(binding->action.u.shell_cmd, sc_val.u.s, sizeof(binding->action.u.shell_cmd) - 1);
                free(sc_val.u.s);
                (*out_count)++;
                continue;
            }

            /* Check for short / long */
            toml_datum_t short_val = toml_string_in(sub_tab, "short");
            if (short_val.ok) {
                action_from_string(short_val.u.s, &binding->action);
                free(short_val.u.s);
                (*out_count)++;
                continue;
            }

            /* Check for gesture directions */
            toml_datum_t up = toml_string_in(sub_tab, "Up");
            toml_datum_t down = toml_string_in(sub_tab, "Down");
            toml_datum_t left = toml_string_in(sub_tab, "Left");
            toml_datum_t right = toml_string_in(sub_tab, "Right");
            toml_datum_t click = toml_string_in(sub_tab, "Click");

            if (up.ok || down.ok || left.ok || right.ok || click.ok) {
                binding->is_gesture = true;
                if (up.ok) { action_from_string(up.u.s, &binding->gesture_up); free(up.u.s); }
                if (down.ok) { action_from_string(down.u.s, &binding->gesture_down); free(down.u.s); }
                if (left.ok) { action_from_string(left.u.s, &binding->gesture_left); free(left.u.s); }
                if (right.ok) { action_from_string(right.u.s, &binding->gesture_right); free(right.u.s); }
                if (click.ok) { action_from_string(click.u.s, &binding->action); free(click.u.s); }
                (*out_count)++;
            }
        }
    }
    return true;
}

static void free_action(openlogi_action_t *action)
{
    if (!action) return;
    if (action->type == ACTION_WORKFLOW && action->u.workflow) {
        free(action->u.workflow);
        action->u.workflow = NULL;
    }
}

static void free_button_binding(config_button_binding_t *b)
{
    if (!b) return;
    free_action(&b->action);
    free_action(&b->gesture_up);
    free_action(&b->gesture_down);
    free_action(&b->gesture_left);
    free_action(&b->gesture_right);
}

static void free_app_profile(config_app_profile_t *prof)
{
    if (!prof) return;
    for (size_t i = 0; i < prof->binding_count; i++) {
        free_button_binding(&prof->bindings[i]);
    }
    prof->binding_count = 0;
}

static void free_device(config_device_t *dev)
{
    if (!dev) return;
    free_app_profile(&dev->default_profile);
    for (size_t i = 0; i < dev->app_profile_count; i++) {
        free_app_profile(&dev->app_profiles[i]);
    }
    dev->app_profile_count = 0;
}

void config_free(openlogi_config_t *cfg)
{
    if (!cfg) return;
    for (size_t i = 0; i < cfg->device_count; i++) {
        free_device(&cfg->devices[i]);
    }
    cfg->device_count = 0;
}

bool config_parse_string(
    const char *toml_str,
    openlogi_config_t *out_config,
    char *errbuf,
    size_t errbufsz)
{
    if (!toml_str || !out_config) {
        if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Null input");
        return false;
    }

    openlogi_config_t *tmp = calloc(1, sizeof(openlogi_config_t));
    if (!tmp) {
        if (errbuf && errbufsz > 0) snprintf(errbuf, errbufsz, "Memory allocation error");
        return false;
    }
    tmp->schema_version = OPENLOGI_SCHEMA_VERSION;

    char parse_err[256];
    toml_table_t *root = toml_parse((char *)toml_str, parse_err, sizeof(parse_err));
    if (!root) {
        if (errbuf && errbufsz > 0) {
            snprintf(errbuf, errbufsz, "TOML parse error: %s", parse_err);
        }
        config_free(tmp);
        free(tmp);
        return false;
    }

    /* Read schema version */
    toml_datum_t ver = toml_int_in(root, "schema_version");
    if (ver.ok) {
        tmp->schema_version = (uint32_t)ver.u.i;
    }

    /* Read selected device */
    toml_datum_t sel_dev = toml_string_in(root, "selected_device");
    if (sel_dev.ok) {
        strncpy(tmp->selected_device, sel_dev.u.s, sizeof(tmp->selected_device) - 1);
        free(sel_dev.u.s);
    }

    /* Read devices table */
    const toml_table_t *devices_tab = toml_table_in(root, "devices");
    if (devices_tab) {
        for (int i = 0; tmp->device_count < OPENLOGI_MAX_DEVICES; i++) {
            const char *dev_key = toml_key_in(devices_tab, i);
            if (!dev_key) break;

            const toml_table_t *dev_tab = toml_table_in(devices_tab, dev_key);
            if (!dev_tab) continue;

            config_device_t *dev = &tmp->devices[tmp->device_count++];
            memset(dev, 0, sizeof(*dev));
            strncpy(dev->device_key, dev_key, sizeof(dev->device_key) - 1);
            dev->enabled = true;
            toml_datum_t enabled = toml_bool_in(dev_tab, "enabled");
            if (enabled.ok) dev->enabled = enabled.u.b;

            toml_datum_t name = toml_string_in(dev_tab, "custom_name");
            if (name.ok) {
                strncpy(dev->custom_name, name.u.s, sizeof(dev->custom_name) - 1);
                free(name.u.s);
            }

            toml_datum_t dpi = toml_int_in(dev_tab, "dpi");
            if (dpi.ok && dpi.u.i > 0 && dpi.u.i <= 32000) {
                dev->dpi = (uint16_t)dpi.u.i;
            }

            const toml_array_t *dpi_arr = toml_array_in(dev_tab, "dpi_presets");
            if (dpi_arr) {
                int arr_sz = toml_array_nelem(dpi_arr);
                for (int d = 0; d < arr_sz && dev->dpi_preset_count < OPENLOGI_MAX_DPI_PRESETS; d++) {
                    toml_datum_t val = toml_int_at(dpi_arr, d);
                    if (val.ok && val.u.i > 0 && val.u.i <= 32000) {
                        dev->dpi_presets[dev->dpi_preset_count++] = (uint16_t)val.u.i;
                    }
                }
            }

            /* SmartShift */
            const toml_table_t *ss_tab = toml_table_in(dev_tab, "smartshift");
            if (ss_tab) {
                toml_datum_t mode = toml_string_in(ss_tab, "mode");
                if (mode.ok) {
                    char m_lower[32];
                    str_tolower(m_lower, mode.u.s, sizeof(m_lower));
                    if (strcmp(m_lower, "ratchet") == 0) dev->smartshift_mode = 1;
                    else if (strcmp(m_lower, "freespin") == 0) dev->smartshift_mode = 2;
                    else if (strcmp(m_lower, "auto") == 0) dev->smartshift_mode = 3;
                    free(mode.u.s);
                }

                toml_datum_t thresh = toml_int_in(ss_tab, "auto_disengage");
                if (thresh.ok && thresh.u.i >= 1 && thresh.u.i <= 255) {
                    dev->smartshift_threshold = (uint8_t)thresh.u.i;
                }
            }

            /* Invert scroll */
            toml_datum_t inv = toml_bool_in(dev_tab, "invert_scroll");
            if (inv.ok) {
                dev->invert_scroll = (bool)inv.u.b;
            }

            /* Default bindings */
            const toml_table_t *bindings_tab = toml_table_in(dev_tab, "bindings");
            if (bindings_tab) {
                if (!parse_button_bindings_table(
                    bindings_tab,
                    dev->default_profile.bindings,
                    &dev->default_profile.binding_count,
                    OPENLOGI_MAX_BINDINGS, parse_err, sizeof(parse_err)
                )) goto invalid_bindings;
            }

            /* Per-application bindings */
            const toml_table_t *per_app_tab = toml_table_in(dev_tab, "per_app_bindings");
            if (per_app_tab) {
                for (int a = 0; dev->app_profile_count < OPENLOGI_MAX_APP_PROFILES; a++) {
                    const char *app_key = toml_key_in(per_app_tab, a);
                    if (!app_key) break;

                    const toml_table_t *app_bindings_tab = toml_table_in(per_app_tab, app_key);
                    if (!app_bindings_tab) continue;

                    config_app_profile_t *prof = &dev->app_profiles[dev->app_profile_count++];
                    memset(prof, 0, sizeof(*prof));
                    strncpy(prof->app_id, app_key, sizeof(prof->app_id) - 1);

                    if (!parse_button_bindings_table(
                        app_bindings_tab,
                        prof->bindings,
                        &prof->binding_count,
                        OPENLOGI_MAX_BINDINGS, parse_err, sizeof(parse_err)
                    )) goto invalid_bindings;
                }
            }
        }
    }

    toml_free(root);
    *out_config = *tmp;
    free(tmp);
    return true;
invalid_bindings:
    if (errbuf && errbufsz) snprintf(errbuf, errbufsz, "%s", parse_err);
    toml_free(root);
    config_free(tmp);
    free(tmp);
    return false;
}

bool config_load_file(
    const char *filepath,
    openlogi_config_t *out_config,
    char *errbuf,
    size_t errbufsz)
{
    if (!filepath || !out_config) return false;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        if (errbuf && errbufsz > 0) {
            snprintf(errbuf, errbufsz, "Could not open file: %s", filepath);
        }
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) { /* 1 MB sanity limit */
        fclose(fp);
        if (errbuf && errbufsz > 0) {
            snprintf(errbuf, errbufsz, "File empty or too large (%ld bytes)", sz);
        }
        return false;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return false;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[read_bytes] = '\0';

    bool ok = config_parse_string(buf, out_config, errbuf, errbufsz);
    free(buf);
    return ok;
}

const config_device_t *config_resolve_device(const openlogi_config_t *cfg, const char *key)
{
    if (!cfg || !cfg->device_count) return NULL;
    if (key && key[0]) {
        for (size_t i = 0; i < cfg->device_count; i++)
            if (strcmp(cfg->devices[i].device_key, key) == 0) return &cfg->devices[i];
    }
    for (size_t i = 0; i < cfg->device_count; i++)
        if (strcmp(cfg->devices[i].device_key, "default") == 0) return &cfg->devices[i];
    return key && key[0] ? NULL : &cfg->devices[0];
}

const config_button_binding_t *config_resolve_binding(
    const openlogi_config_t *cfg,
    const char *device_key,
    const char *current_app_id,
    config_button_id_t button_id)
{
    if (!cfg || cfg->device_count == 0) return NULL;

    const config_device_t *matched_dev = config_resolve_device(cfg, device_key);
    if (!matched_dev || !matched_dev->enabled) return NULL;

    /* 2. Check per-application profile if app_id provided */
    if (current_app_id && current_app_id[0] != '\0') {
        for (size_t p = 0; p < matched_dev->app_profile_count; p++) {
            const config_app_profile_t *prof = &matched_dev->app_profiles[p];
            if (strcmp(prof->app_id, current_app_id) == 0) {
                for (size_t b = 0; b < prof->binding_count; b++) {
                    if (prof->bindings[b].button_id == button_id) {
                        return &prof->bindings[b];
                    }
                }
            }
        }
    }

    /* 3. Fallback to default profile */
    for (size_t b = 0; b < matched_dev->default_profile.binding_count; b++) {
        if (matched_dev->default_profile.bindings[b].button_id == button_id) {
            return &matched_dev->default_profile.bindings[b];
        }
    }

    return NULL;
}
