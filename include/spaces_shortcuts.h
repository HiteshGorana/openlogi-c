/**
 * @file spaces_shortcuts.h
 * @brief macOS Spaces switching, Mission Control, and custom shortcut parser ported from openlogi-inject.
 */

#ifndef OPENLOGI_SPACES_SHORTCUTS_H
#define OPENLOGI_SPACES_SHORTCUTS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual keycodes for macOS */
#define VK_LEFT_ARROW   0x7B
#define VK_RIGHT_ARROW  0x7C
#define VK_DOWN_ARROW   0x7D
#define VK_UP_ARROW     0x7E
#define VK_SPACE        0x31
#define VK_TAB          0x30
#define VK_LEFT_BRACKET 0x21
#define VK_RIGHT_BRACKET 0x1E

/* Modifier bitmasks matching macOS CGEventFlags */
#define MOD_COMMAND  (1ULL << 20)
#define MOD_CONTROL  (1ULL << 18)
#define MOD_OPTION   (1ULL << 19)
#define MOD_SHIFT    (1ULL << 17)

typedef struct {
    uint16_t keycode;
    uint64_t modifiers;
    bool valid;
} parsed_shortcut_t;

/**
 * @brief Map a high-level action name to virtual keycode and modifier flags.
 */
bool spaces_action_to_shortcut(const char *action, parsed_shortcut_t *out_sc);

/**
 * @brief Parse a shortcut string (e.g. "Cmd+Shift+T", "Ctrl+Space") into keycode and modifiers.
 */
bool spaces_parse_custom_shortcut(const char *str, parsed_shortcut_t *out_sc);

/**
 * @brief Look up virtual keycode from character.
 */
uint16_t spaces_char_to_keycode(char c);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_SPACES_SHORTCUTS_H */
