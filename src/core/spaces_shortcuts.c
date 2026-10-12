/**
 * @file spaces_shortcuts.c
 * @brief Implementation of spaces switching and shortcut parser.
 */

#include "spaces_shortcuts.h"

#include <string.h>
#include <ctype.h>

uint16_t spaces_char_to_keycode(char c)
{
    char lower = (char)tolower((unsigned char)c);
    switch (lower) {
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
        case 'k': return 0x28;
        case 'm': return 0x2E;
        case 'n': return 0x2D;
        default:  return 0xFFFF;
    }
}

bool spaces_action_to_shortcut(const char *action, parsed_shortcut_t *out_sc)
{
    if (!action || !out_sc) return false;
    memset(out_sc, 0, sizeof(*out_sc));

    if (strcmp(action, "MissionControl") == 0) {
        out_sc->keycode = VK_UP_ARROW;
        out_sc->modifiers = MOD_CONTROL;
        out_sc->valid = true;
        return true;
    } else if (strcmp(action, "AppExpose") == 0) {
        out_sc->keycode = VK_DOWN_ARROW;
        out_sc->modifiers = MOD_CONTROL;
        out_sc->valid = true;
        return true;
    } else if (strcmp(action, "PreviousDesktop") == 0) {
        out_sc->keycode = VK_LEFT_ARROW;
        out_sc->modifiers = MOD_CONTROL;
        out_sc->valid = true;
        return true;
    } else if (strcmp(action, "NextDesktop") == 0) {
        out_sc->keycode = VK_RIGHT_ARROW;
        out_sc->modifiers = MOD_CONTROL;
        out_sc->valid = true;
        return true;
    } else if (strcmp(action, "BrowserBack") == 0) {
        out_sc->keycode = VK_LEFT_BRACKET;
        out_sc->modifiers = MOD_COMMAND;
        out_sc->valid = true;
        return true;
    } else if (strcmp(action, "BrowserForward") == 0) {
        out_sc->keycode = VK_RIGHT_BRACKET;
        out_sc->modifiers = MOD_COMMAND;
        out_sc->valid = true;
        return true;
    }

    return false;
}

bool spaces_parse_custom_shortcut(const char *str, parsed_shortcut_t *out_sc)
{
    if (!str || !out_sc) return false;
    memset(out_sc, 0, sizeof(*out_sc));

    char buf[128];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(buf, "+", &saveptr);
    while (token != NULL) {
        while (*token == ' ') token++;
        size_t len = strlen(token);
        while (len > 0 && token[len - 1] == ' ') token[--len] = '\0';

        if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0) {
            out_sc->modifiers |= MOD_COMMAND;
        } else if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0) {
            out_sc->modifiers |= MOD_CONTROL;
        } else if (strcasecmp(token, "Opt") == 0 || strcasecmp(token, "Option") == 0 || strcasecmp(token, "Alt") == 0) {
            out_sc->modifiers |= MOD_OPTION;
        } else if (strcasecmp(token, "Shift") == 0) {
            out_sc->modifiers |= MOD_SHIFT;
        } else if (strcasecmp(token, "Space") == 0) {
            out_sc->keycode = VK_SPACE;
            out_sc->valid = true;
        } else if (strcasecmp(token, "Tab") == 0) {
            out_sc->keycode = VK_TAB;
            out_sc->valid = true;
        } else if (strlen(token) == 1) {
            uint16_t kc = spaces_char_to_keycode(token[0]);
            if (kc != 0xFFFF) {
                out_sc->keycode = kc;
                out_sc->valid = true;
            }
        }
        token = strtok_r(NULL, "+", &saveptr);
    }

    return out_sc->valid;
}
