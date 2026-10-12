/**
 * @file inject_macos.c
 * @brief macOS implementation of synthetic input injection.
 */

#ifdef __APPLE__

#include "inject.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NX_KEYTYPE_SOUND_UP    0
#define NX_KEYTYPE_SOUND_DOWN  1
#define NX_KEYTYPE_MUTE        7
#define NX_KEYTYPE_PLAY        16
#define NX_KEYTYPE_NEXT        17
#define NX_KEYTYPE_PREVIOUS    18

#define NX_SUBTYPE_AUX_CONTROL 8
#define NX_KEY_DOWN            0x0A
#define NX_KEY_UP              0x0B

typedef int (*CoreDockSendNotificationFn)(CFStringRef, int);
static CoreDockSendNotificationFn g_core_dock_send = NULL;

bool inject_init(void)
{
    g_core_dock_send = (CoreDockSendNotificationFn)dlsym(RTLD_DEFAULT, "CoreDockSendNotification");
    return true;
}

static void send_dock_notification(const char *name)
{
    if (!g_core_dock_send) {
        g_core_dock_send = (CoreDockSendNotificationFn)dlsym(RTLD_DEFAULT, "CoreDockSendNotification");
    }

    if (g_core_dock_send) {
        CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
        if (str) {
            g_core_dock_send(str, 0);
            CFRelease(str);
        }
    }
}

void inject_mission_control(void)
{
    send_dock_notification("com.apple.expose.awake");
}

void inject_app_expose(void)
{
    send_dock_notification("com.apple.expose.front.awake");
}

void inject_show_desktop(void)
{
    send_dock_notification("com.apple.showdesktop.awake");
}

void inject_launchpad(void)
{
    send_dock_notification("com.apple.launchpad.toggle");
}

void inject_next_space(void)
{
    /* Synthesize Control + Right Arrow (kVK_RightArrow = 0x7C) */
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, 0x7C, true);
    if (down) {
        CGEventSetFlags(down, kCGEventFlagMaskControl);
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);
    }

    CGEventRef up = CGEventCreateKeyboardEvent(NULL, 0x7C, false);
    if (up) {
        CGEventSetFlags(up, kCGEventFlagMaskControl);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }
}

void inject_prev_space(void)
{
    /* Synthesize Control + Left Arrow (kVK_LeftArrow = 0x7B) */
    CGEventRef down = CGEventCreateKeyboardEvent(NULL, 0x7B, true);
    if (down) {
        CGEventSetFlags(down, kCGEventFlagMaskControl);
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);
    }

    CGEventRef up = CGEventCreateKeyboardEvent(NULL, 0x7B, false);
    if (up) {
        CGEventSetFlags(up, kCGEventFlagMaskControl);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }
}

void inject_media_key(media_key_t key)
{
    int nx_key = NX_KEYTYPE_PLAY;
    switch (key) {
        case MEDIA_KEY_PLAY_PAUSE:  nx_key = NX_KEYTYPE_PLAY; break;
        case MEDIA_KEY_NEXT:        nx_key = NX_KEYTYPE_NEXT; break;
        case MEDIA_KEY_PREVIOUS:    nx_key = NX_KEYTYPE_PREVIOUS; break;
        case MEDIA_KEY_VOLUME_UP:   nx_key = NX_KEYTYPE_SOUND_UP; break;
        case MEDIA_KEY_VOLUME_DOWN: nx_key = NX_KEYTYPE_SOUND_DOWN; break;
        case MEDIA_KEY_MUTE:        nx_key = NX_KEYTYPE_MUTE; break;
    }

    Class nsEventClass = objc_getClass("NSEvent");
    if (!nsEventClass) return;

    SEL otherEventSel = sel_registerName("otherEventWithType:location:modifierFlags:timestamp:windowNumber:context:subtype:data1:data2:");
    SEL cgEventSel = sel_registerName("CGEvent");

    typedef id (*OtherEventFn)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, short, intptr_t, long);
    typedef CGEventRef (*CgEventFn)(id, SEL);

    OtherEventFn makeEvent = (OtherEventFn)objc_msgSend;
    CgEventFn getCgEvent = (CgEventFn)objc_msgSend;

    CGPoint pt = {0.0, 0.0};
    int states[2] = { NX_KEY_DOWN, NX_KEY_UP };

    for (int i = 0; i < 2; i++) {
        intptr_t data1 = (nx_key << 16) | (states[i] << 8);
        id nsEvent = makeEvent(
            (id)nsEventClass,
            otherEventSel,
            14, /* NSEventTypeSystemDefined */
            pt,
            0,
            0.0,
            0,
            NULL,
            NX_SUBTYPE_AUX_CONTROL,
            data1,
            0
        );

        if (nsEvent) {
            CGEventRef cgEvent = getCgEvent(nsEvent, cgEventSel);
            if (cgEvent) {
                CGEventPost(kCGHIDEventTap, cgEvent);
            }
        }
    }
}

void inject_key_combo(uint16_t keycode, uint32_t modifiers)
{
    CGEventFlags flags = 0;
    if (modifiers & MOD_CMD)   flags |= kCGEventFlagMaskCommand;
    if (modifiers & MOD_CTRL)  flags |= kCGEventFlagMaskControl;
    if (modifiers & MOD_ALT)   flags |= kCGEventFlagMaskAlternate;
    if (modifiers & MOD_SHIFT) flags |= kCGEventFlagMaskShift;

    CGEventRef down = CGEventCreateKeyboardEvent(NULL, keycode, true);
    if (down) {
        CGEventSetFlags(down, flags);
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);
    }

    CGEventRef up = CGEventCreateKeyboardEvent(NULL, keycode, false);
    if (up) {
        CGEventSetFlags(up, flags);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }
}

void inject_mouse_click(uint32_t button)
{
    CGEventRef dummy = CGEventCreate(NULL);
    if (!dummy) return;
    CGPoint loc = CGEventGetLocation(dummy);
    CFRelease(dummy);

    CGEventType down_type = kCGEventOtherMouseDown;
    CGEventType up_type = kCGEventOtherMouseUp;
    CGMouseButton mouse_btn = kCGMouseButtonCenter;

    if (button == 1) {
        down_type = kCGEventLeftMouseDown;
        up_type = kCGEventLeftMouseUp;
        mouse_btn = kCGMouseButtonLeft;
    } else if (button == 2) {
        down_type = kCGEventRightMouseDown;
        up_type = kCGEventRightMouseUp;
        mouse_btn = kCGMouseButtonRight;
    } else {
        mouse_btn = (CGMouseButton)(button - 1);
    }

    CGEventRef down = CGEventCreateMouseEvent(NULL, down_type, loc, mouse_btn);
    if (down) {
        if (button > 2) {
            CGEventSetIntegerValueField(down, kCGMouseEventButtonNumber, button - 1);
        }
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);
    }

    CGEventRef up = CGEventCreateMouseEvent(NULL, up_type, loc, mouse_btn);
    if (up) {
        if (button > 2) {
            CGEventSetIntegerValueField(up, kCGMouseEventButtonNumber, button - 1);
        }
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }
}

void inject_scroll(double dx, double dy)
{
    CGEventRef scroll = CGEventCreateScrollWheelEvent2(
        NULL,
        kCGScrollEventUnitPixel,
        2,
        (int32_t)dy,
        (int32_t)dx,
        0
    );

    if (scroll) {
        CGEventPost(kCGHIDEventTap, scroll);
        CFRelease(scroll);
    }
}

void inject_text(const char *text)
{
    if (!text || text[0] == '\0') return;

    size_t len = strlen(text);
    UniChar *chars = (UniChar *)malloc(sizeof(UniChar) * len);
    if (!chars) return;

    for (size_t i = 0; i < len; i++) {
        chars[i] = (UniChar)text[i];
    }

    CGEventRef event = CGEventCreateKeyboardEvent(NULL, 0, true);
    if (event) {
        CGEventKeyboardSetUnicodeString(event, (CFIndex)len, chars);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }

    free(chars);
}

void inject_action(const openlogi_action_t *action)
{
    if (!action) return;

    switch (action->type) {
        case ACTION_NONE:
            break;
        case ACTION_KEY_COMBO:
            inject_key_combo(action->u.key_combo.keycode, action->u.key_combo.modifiers);
            break;
        case ACTION_MEDIA_KEY:
            inject_media_key(action->u.media_key);
            break;
        case ACTION_MISSION_CONTROL:
            inject_mission_control();
            break;
        case ACTION_APP_EXPOSE:
            inject_app_expose();
            break;
        case ACTION_SHOW_DESKTOP:
            inject_show_desktop();
            break;
        case ACTION_LAUNCHPAD:
            inject_launchpad();
            break;
        case ACTION_NEXT_SPACE:
            inject_next_space();
            break;
        case ACTION_PREV_SPACE:
            inject_prev_space();
            break;
        case ACTION_MOUSE_CLICK:
            inject_mouse_click(action->u.mouse_click.button);
            break;
        case ACTION_SCROLL:
            inject_scroll(action->u.scroll.dx, action->u.scroll.dy);
            break;
        case ACTION_TEXT:
            inject_text(action->u.text);
            break;
        case ACTION_SHELL_COMMAND:
            if (action->u.shell_cmd[0] != '\0') {
                int res = system(action->u.shell_cmd);
                (void)res;
            }
            break;
        case ACTION_APP_OPEN:
            if (action->u.app_path[0] != '\0') {
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "open \"%s\" > /dev/null 2>&1", action->u.app_path);
                int res = system(cmd);
                (void)res;
            }
            break;
        case ACTION_APPLE_SCRIPT:
            if (action->u.apple_script[0] != '\0') {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "osascript -e '%s' > /dev/null 2>&1", action->u.apple_script);
                int res = system(cmd);
                (void)res;
            }
            break;
        case ACTION_SLEEP:
            {
                int res = system("pmset sleepnow > /dev/null 2>&1");
                (void)res;
            }
            break;
        case ACTION_LOCK_SCREEN:
            inject_key_combo(0x0C, MOD_CMD | MOD_CTRL); /* Cmd+Ctrl+Q */
            break;
        case ACTION_SCREENSHOT:
            inject_key_combo(0x14, MOD_CMD | MOD_SHIFT); /* Cmd+Shift+3 */
            break;
        case ACTION_CAPTURE_REGION:
            inject_key_combo(0x15, MOD_CMD | MOD_CTRL | MOD_SHIFT); /* Cmd+Ctrl+Shift+4 */
            break;
        case ACTION_CYCLE_DPI:
        case ACTION_SET_DPI_PRESET:
        case ACTION_TOGGLE_SMARTSHIFT:
        case ACTION_SHOW_ACTIONS_RING:
            /* Handled at runtime/daemon level */
            break;
        case ACTION_WORKFLOW:
            if (action->u.workflow) {
                for (size_t i = 0; i < action->u.workflow->step_count; i++) {
                    const workflow_step_t *step = &action->u.workflow->steps[i];
                    switch (step->type) {
                        case WORKFLOW_STEP_PRESS_KEY:
                            inject_key_combo(step->u.key.keycode, step->u.key.modifiers);
                            break;
                        case WORKFLOW_STEP_DELAY:
                            usleep(step->u.delay_ms * 1000);
                            break;
                        case WORKFLOW_STEP_TYPE_TEXT:
                            inject_text(step->u.text);
                            break;
                        case WORKFLOW_STEP_APPLE_SCRIPT:
                            if (step->u.script[0] != '\0') {
                                char cmd[1024];
                                snprintf(cmd, sizeof(cmd), "osascript -e '%s' > /dev/null 2>&1", step->u.script);
                                int res = system(cmd);
                                (void)res;
                            }
                            break;
                        case WORKFLOW_STEP_SHELL_CMD:
                            if (step->u.cmd[0] != '\0') {
                                int res = system(step->u.cmd);
                                (void)res;
                            }
                            break;
                    }
                }
            }
            break;
    }
}

#endif /* __APPLE__ */
