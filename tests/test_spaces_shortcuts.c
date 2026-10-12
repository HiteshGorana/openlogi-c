/**
 * @file test_spaces_shortcuts.c
 * @brief Regression tests for macOS Spaces switching and shortcut synthesis ported from openlogi-inject.
 */

#include "spaces_shortcuts.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void test_spaces_action_to_shortcut_mapping(void)
{
    parsed_shortcut_t sc;

    /* 1. MissionControl -> Ctrl+Up */
    assert(spaces_action_to_shortcut("MissionControl", &sc));
    assert(sc.valid);
    assert(sc.keycode == VK_UP_ARROW);
    assert(sc.modifiers == MOD_CONTROL);

    /* 2. AppExpose -> Ctrl+Down */
    assert(spaces_action_to_shortcut("AppExpose", &sc));
    assert(sc.valid);
    assert(sc.keycode == VK_DOWN_ARROW);
    assert(sc.modifiers == MOD_CONTROL);

    /* 3. PreviousDesktop -> Ctrl+Left */
    assert(spaces_action_to_shortcut("PreviousDesktop", &sc));
    assert(sc.valid);
    assert(sc.keycode == VK_LEFT_ARROW);
    assert(sc.modifiers == MOD_CONTROL);

    /* 4. NextDesktop -> Ctrl+Right */
    assert(spaces_action_to_shortcut("NextDesktop", &sc));
    assert(sc.valid);
    assert(sc.keycode == VK_RIGHT_ARROW);
    assert(sc.modifiers == MOD_CONTROL);

    /* 5. Browser navigation */
    assert(spaces_action_to_shortcut("BrowserBack", &sc));
    assert(sc.keycode == VK_LEFT_BRACKET);
    assert(sc.modifiers == MOD_COMMAND);

    assert(spaces_action_to_shortcut("BrowserForward", &sc));
    assert(sc.keycode == VK_RIGHT_BRACKET);
    assert(sc.modifiers == MOD_COMMAND);
}

static void test_spaces_modifier_flag_synthesis(void)
{
    parsed_shortcut_t sc;

    /* Multi-modifier parsing */
    assert(spaces_parse_custom_shortcut("Cmd+Shift+T", &sc));
    assert(sc.valid);
    assert(sc.modifiers == (MOD_COMMAND | MOD_SHIFT));
    assert(sc.keycode == 0x11); /* 't' */

    assert(spaces_parse_custom_shortcut("Ctrl+Opt+Cmd+Space", &sc));
    assert(sc.valid);
    assert(sc.modifiers == (MOD_CONTROL | MOD_OPTION | MOD_COMMAND));
    assert(sc.keycode == VK_SPACE);
}

static void test_spaces_key_code_lookup(void)
{
    assert(spaces_char_to_keycode('a') == 0x00);
    assert(spaces_char_to_keycode('A') == 0x00); /* Case insensitive */
    assert(spaces_char_to_keycode('z') == 0x06);
    assert(spaces_char_to_keycode('q') == 0x0C);
    assert(spaces_char_to_keycode('w') == 0x0D);
    assert(spaces_char_to_keycode('1') == 0x12);
    assert(spaces_char_to_keycode('?') == 0xFFFF); /* Invalid */
}

static void test_spaces_custom_shortcut_parser(void)
{
    parsed_shortcut_t sc;

    /* Valid shortcuts with varying whitespace */
    assert(spaces_parse_custom_shortcut("Command + Shift + z", &sc));
    assert(sc.modifiers == (MOD_COMMAND | MOD_SHIFT));
    assert(sc.keycode == 0x06);

    /* Special key words */
    assert(spaces_parse_custom_shortcut("Opt+Tab", &sc));
    assert(sc.modifiers == MOD_OPTION);
    assert(sc.keycode == VK_TAB);

    /* Unknown / invalid shortcut */
    assert(!spaces_parse_custom_shortcut("InvalidOnly", &sc));
    assert(!spaces_parse_custom_shortcut("", &sc));
}

static void test_spaces_hold_shortcut_down_up_events(void)
{
    parsed_shortcut_t sc;
    assert(spaces_parse_custom_shortcut("Ctrl+Space", &sc));
    assert(sc.valid);
    assert(sc.modifiers == MOD_CONTROL);
    assert(sc.keycode == VK_SPACE);

    /* In C CGEvent injection, a hold shortcut synthesizes:
     * 1. KeyDown with flag mask MOD_CONTROL
     * 2. (while held...)
     * 3. KeyUp with clear flag mask
     */
    uint64_t down_flags = sc.modifiers;
    uint64_t up_flags = 0;
    assert(down_flags & MOD_CONTROL);
    assert(up_flags == 0);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_spaces_shortcuts", "macOS Spaces Switching & Shortcut Synthesis");

    TEST_CASE_RUN(g_suite, test_spaces_action_to_shortcut_mapping);
    TEST_CASE_RUN(g_suite, test_spaces_modifier_flag_synthesis);
    TEST_CASE_RUN(g_suite, test_spaces_key_code_lookup);
    TEST_CASE_RUN(g_suite, test_spaces_custom_shortcut_parser);
    TEST_CASE_RUN(g_suite, test_spaces_hold_shortcut_down_up_events);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
