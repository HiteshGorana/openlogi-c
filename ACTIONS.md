# Actions and supported controls

OpenLogi's Rust implementation defines **53 actions**: 45 entries in the shared
action picker and 8 actions that take a shortcut, preset index, text, script,
workflow, or application target. Mouse controls and keyboard keys use the same
action vocabulary. Their differences are capture support, device capabilities,
and whether an input has a physical press/release lifecycle.

This reference describes the current source, including actions that appear in
the picker but have platform limitations. The agent must be running for bindings
to work. For config locations and device identifiers, see
[Configuration](docs/CONFIGURATION.md).

## Which controls can run which actions?

Ordinary actions—mouse clicks, editing, browser navigation, media, scrolling,
screen locking, and screenshots—can be assigned to any captured mouse button,
thumb-wheel direction, supported keyboard control, or global function-key
trigger. The operating system must also implement the chosen action; see the
platform tables below.

| Action family | Mouse buttons | Thumb-wheel rotation | HID++ keyboard keys | Global Esc / F-key triggers |
| --- | --- | --- | --- | --- |
| Clicks, editing, browser, navigation, system, media, scrolling | Yes | Yes | Yes | Yes, on macOS and Windows |
| `CustomShortcut`, `OpenApplication`, text, scripts, workflows | Yes | Yes | Yes | Yes, on macOS and Windows |
| `HoldShortcut` | Hold until release where press/release is captured | Balanced tap; no held rotation | Hold until release | Hold until release on macOS and Windows |
| `CycleDpiPresets`, `SetDpiPreset`, `ToggleSmartShift` | Requires a compatible target mouse | Requires a compatible target mouse | Targets the originating keyboard, so generally has no useful effect | Targets the selected device; select a compatible mouse |
| `ShowActionsRing` | Yes | Yes; rotation can trigger it repeatedly | Yes | Yes, on macOS and Windows |
| `None` | Suppresses a captured input | Produces no action | A single `None` binding leaves the firmware function active | Suppresses the matching key-down action |

Device actions do not automatically look for another mouse. A HID++ input
targets its originating device; an OS-hook input uses the selected device.
DPI actions need configured presets and writable DPI support. SmartShift needs
a wheel that supports changing its ratchet/free-spin mode.

### Mouse controls

The default column describes a fresh OpenLogi binding, not a promise that the
physical control exists on every mouse. HID++ controls must be exposed by the
device and support diversion; gesture capture also needs raw movement reports.

| Physical control | TOML binding key | Default | Capture / support | Gesture mode |
| --- | --- | --- | --- | --- |
| Primary click | `LeftClick` | `LeftClick` | Always passes through the OS hook; no remapping control in the GUI | No |
| Secondary click | `RightClick` | `RightClick` | Always passes through the OS hook; no remapping control in the GUI | No |
| Main wheel click | `MiddleClick` | `MiddleClick` | OS hook; HID++ where the device exposes diversion | No new gesture assignments |
| Side Back button | `Back` | `MouseBack` | OS hook or HID++ | Yes |
| Side Forward button | `Forward` | `MouseForward` | OS hook or HID++ | Yes |
| ModeShift / DPI button | `DpiToggle` | `CycleDpiPresets` | HID++; the name does not require assigning a DPI action | Yes, with raw-XY support |
| Thumb-wheel capacitive tap | `Thumbwheel` | `None` | HID++; TOML only, with no GUI hotspot | No |
| Thumb wheel forward / up rotation | `ThumbwheelScrollUp` | `HorizontalScrollLeft` | HID++; GUI shows both directions in one wheel control | No |
| Thumb wheel backward / down rotation | `ThumbwheelScrollDown` | `HorizontalScrollRight` | HID++; GUI shows both directions in one wheel control | No |
| Dedicated gesture button | `GestureButton` | Five-direction map below | HID++ | Yes, with raw-XY support; enabled by default |
| MX Master 4 Haptic Sense Panel | `HapticPanel` | `ShowActionsRing` | HID++; separate from the gesture button | Yes, with raw-XY support |
| Main wheel tilted left | `WheelTiltLeft` | `HorizontalScrollLeft` | HID++; native firmware scrolling remains active at the default | No |
| Main wheel tilted right | `WheelTiltRight` | `HorizontalScrollRight` | HID++; native firmware scrolling remains active at the default | No |

`LeftClick` and `RightClick` are valid **output actions** for other buttons or
keyboard keys. Their presence in the config schema does not make the physical
primary buttons remappable through the OS hook.

The main wheel's vertical rotation has no `ButtonId` for assigning arbitrary
actions. Its sensitivity, inversion, resolution, and smooth scrolling are
settings. The horizontal thumb wheel has separate directional action bindings.
Scroll actions on that wheel use continuous, sensitivity-scaled output; other
actions fire as rotation accumulates and can repeat. A capacitive tap can also
be reported from incidental thumb contact, which is why it defaults to `None`.

### Gesture directions and press modes

Gesture mode provides five action slots: `Click`, `Up`, `Down`, `Left`, and
`Right`. Holding an eligible button and moving the mouse chooses a swipe
direction; a press without a committed swipe chooses `Click`. Multiple eligible
buttons can have independent gesture maps.

| Direction | Default action |
| --- | --- |
| `Click` | `AppExpose` |
| `Up` | `MissionControl` |
| `Down` | `ShowDesktop` |
| `Left` | `PrevTab` |
| `Right` | `NextTab` |

These are the dedicated gesture button's fresh defaults. Promoting a different
button preserves its existing action as `Click` and fills the other directions
from these defaults.

| Input source | Single action | Held shortcut | Short / long pair | Gesture direction map |
| --- | --- | --- | --- | --- |
| Captured mouse button with down/up events | Yes | Until release | Yes, in device TOML bindings | Only eligible controls listed above |
| HID++ keyboard control | Yes | Until release | Yes, in device TOML bindings | No supported keyboard gesture mode |
| Instantaneous button pulse / thumb-wheel tap | Yes | Balanced tap | Runs `short` | No |
| Thumb-wheel rotation | Yes | Balanced tap | Uses the projected `short` action | No |
| Global Esc / F-key trigger | Yes | Until release | No | No |
| Actions Ring slot | Yes | Balanced tap | No | No |

A short/long binding releases before **500 ms** to run `short`, or stays down
for 500 ms to run `long` once. The later release does not also run `short`.
Interrupted presses are cancelled. The GUI displays the short action; assigning
a new action there replaces the complete pair. Per-application overlays and
global keyboard triggers accept one action, not a short/long pair or a direction
map. Gesture mode and short/long mode are alternative binding shapes.

`HoldShortcut` releases its output on physical release, capture cancellation,
binding invalidation, or agent shutdown. A gesture-assigned held shortcut lasts
only for the remainder of its originating press; a click outcome at release
cannot provide a sustained hold. Legacy middle-click gesture configs still
have a runtime path, but the GUI does not offer new middle-click gestures.

## Keyboard keys

OpenLogi has two keyboard binding paths. The Keys tab uses per-device HID++
controls when the keyboard's asset metadata contains control markers. Without
those markers it shows the global Esc / function-key row, with the marked
subset where available or the full fallback row otherwise. A displayed marker
alone does not prove the firmware can divert that key.

### Per-device HID++ controls

These bind under `[devices."<physical-key>".bindings]`, with optional
per-application action overrides. Any ordinary action can replace a key's
printed function; a Play/Pause key can run `Copy`, and a Screen Capture key can
run `PlayPause`.

The following **43 named controls** are recognized. Each works only if the
keyboard advertises that CID as divertable through HID++ feature `0x1b04`.
The label describes the physical control, not a dedicated OpenLogi action.

| Physical key / function | TOML binding key | HID++ CID |
| --- | --- | --- |
| Brightness down | `KeyBrightnessDown` | `0x00c7` |
| Brightness up | `KeyBrightnessUp` | `0x00c8` |
| Keyboard backlight down | `KeyBacklightDown` | `0x00e2` |
| Keyboard backlight up | `KeyBacklightUp` | `0x00e3` |
| Mission Control | `KeyMissionControl` | `0x00e0` |
| Launchpad | `KeyLaunchpad` | `0x00e1` |
| Show desktop | `KeyShowDesktop` | `0x006e` |
| Search | `KeySearch` | `0x00d4` |
| Dictation | `KeyDictation` | `0x0103` |
| Emoji | `KeyEmoji` | `0x0108` |
| Screen capture | `KeyScreenCapture` | `0x010a` |
| Print Screen | `KeyPrintScreen` | `0x00bf` |
| Microphone mute | `KeyMicMute` | `0x011c` |
| Previous track | `KeyPreviousTrack` | `0x00e4` |
| Play / pause | `KeyPlayPause` | `0x00e5` |
| Play / pause, alternate track control | `KeyPlayPauseTrack` | `0x0141` |
| Next track | `KeyNextTrack` | `0x00e6` |
| Mute | `KeyMute` | `0x00e7` |
| Volume down | `KeyVolumeDown` | `0x00e8` |
| Volume up | `KeyVolumeUp` | `0x00e9` |
| Calculator | `KeyCalculator` | `0x000a` |
| Lock | `KeyLock` | `0x006f` |
| Do Not Disturb | `KeyDoNotDisturb` | `0x011d` |
| Heart-eyes emoji | `KeyEmojiHeartEyes` | `0x0104` |
| Crying emoji | `KeyEmojiCrying` | `0x0105` |
| Smiley emoji | `KeyEmojiSmiley` | `0x0106` |
| Tears-of-joy emoji | `KeyEmojiTears` | `0x0107` |
| Look up | `KeyLookUp` | `0x00ea` |
| App switch | `KeyAppSwitch` | `0x0100` |
| App switch / Launchpad | `KeyAppSwitchLaunchpad` | `0x0109` |
| Home | `KeyHome` | `0x0118` |
| End | `KeyEnd` | `0x0119` |
| Page Up | `KeyPageUp` | `0x011a` |
| Page Down | `KeyPageDown` | `0x011b` |
| App switch, multiplatform | `KeyMultiPlatformAppSwitch` | `0x00ba` |
| Home, multiplatform | `KeyMultiPlatformHome` | `0x00bb` |
| Menu, multiplatform | `KeyMultiPlatformMenu` | `0x00bc` |
| Insert, multiplatform | `KeyMultiPlatformInsert` | `0x00be` |
| Language switch | `KeyLanguageSwitch` | `0x00dd` |
| Circle, POP Icon Keys | `KeyCircle` | `0x01a3` |
| Triangle, POP Icon Keys | `KeyTriangle` | `0x01a4` |
| Diamond, POP Icon Keys | `KeyDiamond` | `0x01a5` |
| Star, POP Icon Keys | `KeyStar` | `0x01a6` |

Unknown or unnamed controls use a quoted numeric key such as
`"control:0x01f3"`. Named controls also accept their numeric spelling, but using
both names for the same CID in one table is rejected. Run
`openlogi diag controls` to inspect the connected device's controls and flags.

Unbound controls keep their firmware behavior. A single `None` action also
leaves them native; it does not disable the key. Short/long pairs are diverted
even if an arm is `None`, so that arm can deliberately produce no output.
Easy-Switch host keys (`0x00d1`–`0x00d3`) are reserved for host switching, and
integrated-touchpad primary clicks (`0x0050`, `0x0051`) are never diverted as
keyboard keys. Even a numeric binding cannot override these reservations.

HID++ bindings act on the media/shortcut function of the top row. With Fn-lock
off, pressing the key reaches that function; with Fn-lock on, it generally takes
Fn+key. Plain F-key events use the separate OS-hook path. Fn itself cannot be a
global trigger modifier. The current agent maintains one HID++ keyboard capture
session at a time, preferring an online keyboard with bindings.

### Global Esc and function-key triggers

| Trigger keys | TOML section | macOS | Linux | Windows |
| --- | --- | --- | --- | --- |
| `esc`, `f1` through `f19` | `[keyboard.bindings]` | Captured | Keyboard events are not captured by the Linux OS hook | Captured |
| Those keys with modifiers | `[keyboard.bindings]` | Captured | Same limitation | Captured |

Trigger names are case-insensitive. Supported modifiers are `shift`,
`control` / `ctrl`, `option` / `alt`, and `command` / `cmd`. The modifier mask
must match exactly. On Windows, the trigger's `command` flag means the Windows
key; this differs from the cross-platform `Cmd` modifier in an output shortcut.

These bindings apply across keyboards and have no per-device or per-app
selection. Letters, digits, arrows, F20+, and arbitrary typing keys are not
supported global triggers. The OS hook must be enabled; setting
`app_settings.capture_mouse_events = false` also disables this keyboard path.

## Complete action catalog

Action identifiers below are the exact, case-sensitive TOML values. All 45
entries in this section appear in the shared mouse and keyboard action picker.

### Mouse, editing, browser, media, and scrolling

`Cmd` in the macOS column means Command. Application shortcuts still depend on
the receiving application supporting the corresponding command.

| Action | Effect / macOS output | Linux / Windows output |
| --- | --- | --- |
| `LeftClick` | Primary mouse click | Primary mouse click |
| `RightClick` | Secondary mouse click | Secondary mouse click |
| `MiddleClick` | Wheel / middle mouse click | Wheel / middle mouse click |
| `MouseBack` | Native mouse button 4 | Native side / XBUTTON1 click |
| `MouseForward` | Native mouse button 5 | Native extra / XBUTTON2 click |
| `Copy` | Cmd+C | Ctrl+C |
| `Paste` | Cmd+V | Ctrl+V |
| `Cut` | Cmd+X | Ctrl+X |
| `Undo` | Cmd+Z | Ctrl+Z |
| `Redo` | Cmd+Shift+Z | Linux: Ctrl+Shift+Z; Windows: Ctrl+Y |
| `SelectAll` | Cmd+A | Ctrl+A |
| `Find` | Cmd+F | Ctrl+F |
| `Save` | Cmd+S | Ctrl+S |
| `BrowserBack` | Browser navigation; Cmd+[ fallback | Linux: Alt+Left; Windows: browser-back virtual key |
| `BrowserForward` | Browser navigation; Cmd+] fallback | Linux: Alt+Right; Windows: browser-forward virtual key |
| `NewTab` | Cmd+T | Ctrl+T |
| `CloseTab` | Cmd+W | Ctrl+W |
| `ReopenTab` | Cmd+Shift+T | Ctrl+Shift+T |
| `NextTab` | Ctrl+Tab | Ctrl+Tab |
| `PrevTab` | Ctrl+Shift+Tab | Ctrl+Shift+Tab |
| `ReloadPage` | Cmd+R | Ctrl+R |
| `PlayPause` | Media play / pause | Linux: MPRIS PlayPause; Windows: media key |
| `NextTrack` | Next track | Linux: MPRIS Next; Windows: media key |
| `PrevTrack` | Previous track | Linux: MPRIS Previous; Windows: media key |
| `VolumeUp` | Increase system volume | Volume-up key |
| `VolumeDown` | Decrease system volume | Volume-down key |
| `MuteVolume` | Toggle system audio mute | Volume-mute key; not microphone mute |
| `ScrollUp` | Vertical scroll up | Vertical scroll up |
| `ScrollDown` | Vertical scroll down | Vertical scroll down |
| `HorizontalScrollLeft` | Horizontal scroll left | Horizontal scroll left |
| `HorizontalScrollRight` | Horizontal scroll right | Horizontal scroll right |

`MouseBack` / `MouseForward` emit mouse clicks. `BrowserBack` /
`BrowserForward` execute navigation instead. On macOS, browser navigation uses
Safari's Accessibility toolbar target where available, or the frontmost app's
menu/keyboard shortcut path elsewhere.

### Navigation, system, and device actions

| Action | macOS | Linux | Windows |
| --- | --- | --- | --- |
| `MissionControl` | Dock Mission Control | GNOME Activities via Super; skipped outside GNOME | Task View via Win+Tab |
| `AppExpose` | Dock shows the front app's windows | Skipped; no implemented equivalent | Task View via Win+Tab; shows all apps |
| `PreviousDesktop` | Switch to previous Space | Ctrl+Alt+Left | Win+Ctrl+Left |
| `NextDesktop` | Switch to next Space | Ctrl+Alt+Right | Win+Ctrl+Right |
| `ShowDesktop` | Dock shows the desktop | GNOME Super+D; skipped outside GNOME | Win+D |
| `LaunchpadShow` | Dock Launchpad toggle where available | GNOME Super+A; skipped outside GNOME | Open Start via the Windows key |
| `None` | No output from a captured action | Same | Same |
| `LockScreen` | Cmd+Ctrl+Q | logind locks the current session; Super+L fallback | Win+L |
| `Screenshot` | System screenshot-to-file hotkey, normally Cmd+Shift+3 | Print Screen; desktop handler decides the result | Win+Shift+S snipping overlay |
| `CaptureRegion` | System selected-area-to-clipboard hotkey, normally Cmd+Ctrl+Shift+4 | Desktop-specific region-capture chord | Win+Shift+S snipping overlay |
| `Sleep` | `pmset sleepnow` | logind Suspend; requires a usable system bus and authorization | Skipped; not implemented |
| `ShowActionsRing` | OpenLogi's eight-slot ring at the pointer | Same | Same |
| `CycleDpiPresets` | Advance through the target device's configured DPI presets, wrapping at the end | Same HID++ action | Same HID++ action |
| `ToggleSmartShift` | Toggle the target device's ratchet / free-spin wheel mode | Same HID++ action | Same HID++ action |

Linux desktop shortcuts depend on the desktop's bindings. Region capture uses
Print Screen for GNOME or an unrecognized desktop, Shift+Print Screen for
Cinnamon/MATE/XFCE, and Super+Shift+Print Screen for KDE. The macOS Launchpad
backend documents its toggle as a no-op on macOS 26.

### Actions with parameters

These 8 actions are valid bindings but are outside the fixed catalog.

| Action | Parameter / behavior | GUI authoring | Platform support |
| --- | --- | --- | --- |
| `SetDpiPreset` | Zero-based preset index; `0` selects preset 1. An index beyond the list selects its last preset | TOML | Compatible target device with DPI presets |
| `CustomShortcut` | One key plus optional modifiers; sends a balanced down/up pair | Mouse shortcut editor and ring shortcut editor; keyboard via TOML | macOS, Linux, Windows |
| `HoldShortcut` | One key plus optional modifiers; holds output for the physical press where supported | Mouse shortcut editor's Hold mode; keyboard and ring via TOML | macOS, Linux, Windows; ring/wheel dispatch is a tap |
| `OpenApplication` | Target path or URL, optionally with a display name; opens applications, folders, files, or URLs | Mouse and ring path editors; keyboard via TOML | macOS, Linux, Windows; target must exist or have a handler |
| `TypeText` | Unicode string to type | Keyboard Power User editor; mouse and ring via TOML | Implemented on macOS; logs a warning without typing on Linux/Windows |
| `RunAppleScript` | AppleScript source | Keyboard Power User editor; mouse and ring via TOML | macOS via `osascript`; logs a warning on Linux/Windows |
| `RunShellCommand` | Command string | Keyboard Power User editor; mouse and ring via TOML | `/bin/sh -c` on macOS/Linux; `cmd /C` on Windows |
| `Workflow` | Ordered sequence of steps | Keyboard Power User editor; mouse and ring via TOML | Runs on all three platforms; each step retains its platform limits |

Supported workflow steps are `TypeText`, `PressKey`, `Delay` (milliseconds),
`RunAppleScript`, and `RunShellCommand`. A workflow runs its steps in order on
a background thread. An unsupported step logs and continues to the next step.
It cannot directly contain arbitrary catalog actions such as DPI changes or
`ShowActionsRing`.

### Shortcut output keys

`CustomShortcut`, `HoldShortcut`, and workflow `PressKey` share one parser:

| Output keys | Accepted names |
| --- | --- |
| Letters / digits | A–Z, 0–9 |
| Function keys | F1–F20 |
| Editing / whitespace | Enter / Return, Escape / Esc, Backspace, Tab, Space, Delete |
| Navigation | Home, End, PageUp / Page-Up, PageDown / Page-Down, Left, Right, Up, Down |
| Punctuation | Minus, equals, brackets, backslash, semicolon, apostrophe, backtick, comma, period, slash, entered as their literal characters |

A chord needs exactly one non-modifier key. Modifier-only chords, multiple
ordinary keys in one chord, keypad-specific keys, and unsupported key names
are rejected. F20 is an output key even though global triggers stop at F19.

| Output modifier | macOS | Linux | Windows |
| --- | --- | --- | --- |
| `Cmd` / `Command` | Command | Control | Control |
| `Ctrl` / `Control` | Control | Control | Control |
| `Alt` / `Option` | Option | Alt | Alt |
| `Shift` | Shift | Shift | Shift |
| `Super` / `Win` / `Meta` | Command | Left Meta | Windows key |

Use `Cmd+C` for a portable copy shortcut and `Win+L` for a Windows logo-key
shortcut. Trigger modifiers and output modifiers are separate vocabularies.

## Actions Ring and application profiles

The ring has eight slots: `Top`, `TopRight`, `Right`, `BottomRight`, `Bottom`,
`BottomLeft`, `Left`, and `TopLeft`. Default actions, clockwise from the top,
are Cut, Copy, Paste, BrowserForward, PlayPause, BrowserBack, Undo, and Redo.

A slot accepts any action except `ShowActionsRing` (recursive rings are
rejected) and `None` (remove the slot entry to leave it empty). Held shortcuts
in slots run as balanced taps. Per-application ring layouts are complete
replacements of the default layout.

Per-device mouse and HID++ keyboard bindings support sparse per-application
action overrides. Mouse profiles follow the window under the pointer by
default; keyboard profiles follow the focused app. Mouse actions that send
keystrokes, type text, execute scripts/workflows, or show App Exposé require
the identified hovered window to be focused. OpenLogi skips those actions
rather than activating a background window. Global actions such as desktop
switching can run without changing focus. Unsupported pointer-targeting
sessions use the focused app; see [Configuration](docs/CONFIGURATION.md).

## TOML examples

Merge these fragments into the existing config and replace the placeholder
device keys with OpenLogi's actual physical keys. They are examples of the
binding shapes, not a complete config file.

```toml
[devices."<mouse-physical-key>".bindings]
Back = "Copy"
Forward = "Paste"
MiddleClick = { HoldShortcut = "Ctrl+Space" }
DpiToggle = { short = "CycleDpiPresets", long = "ShowActionsRing" }
WheelTiltLeft = { CustomShortcut = "Ctrl+Shift+Tab" }
WheelTiltRight = { CustomShortcut = "Ctrl+Tab" }
ThumbwheelScrollUp = "HorizontalScrollLeft"
ThumbwheelScrollDown = "HorizontalScrollRight"

[devices."<mouse-physical-key>".bindings.GestureButton]
Click = "AppExpose"
Up = "MissionControl"
Down = "ShowDesktop"
Left = "PreviousDesktop"
Right = "NextDesktop"

[devices."<keyboard-physical-key>".bindings]
KeyScreenCapture = "CaptureRegion"
KeyPlayPause = "PlayPause"
KeyCalculator = { OpenApplication = { path = "/System/Applications/Calculator.app", display_name = "Calculator" } }
KeyMicMute = { HoldShortcut = "Ctrl+Space" }
"control:0x01f3" = { CustomShortcut = "Cmd+Shift+P" }

[keyboard.bindings]
f1 = "Copy"
"shift+f2" = "Paste"
"ctrl+f5" = { CustomShortcut = "Cmd+Shift+P" }
f6 = { SetDpiPreset = 0 }
f7 = { HoldShortcut = "Ctrl+Space" }
f8 = { RunShellCommand = "echo hello" }
f9 = { Workflow = [{ PressKey = "Cmd+C" }, { Delay = { millis = 250 } }, { PressKey = "Cmd+V" }] }

[devices."<mouse-physical-key>".action_ring.default.slots]
Top = { action = "Copy" }
Right = { action = { CustomShortcut = "Cmd+Shift+P" }, label = "Command Palette" }
Bottom = { action = { OpenApplication = { path = "~/Downloads", display_name = "Downloads" } } }
```

The Calculator path is a macOS example. `KeyMicMute` here is a push-to-talk
shortcut example, not built-in microphone mute. Brightness, backlight,
dictation, emoji, Calculator, and Do Not Disturb are named input controls;
there are no dedicated catalog actions for those functions. Leave their
firmware behavior active or assign an appropriate shortcut/application/script.

## Source references

- [Action vocabulary and shared picker catalog](crates/openlogi-core/src/binding/action.rs)
- [Mouse control identifiers and gesture eligibility](crates/openlogi-core/src/binding/button.rs)
- [Default actions and gesture map](crates/openlogi-core/src/binding/defaults.rs)
- [Keyboard control catalog](crates/openlogi-core/src/binding/control.rs)
- [Global function keys](crates/openlogi-core/src/config/function_key.rs) and [trigger parser](crates/openlogi-core/src/config/key_trigger.rs)
- [Shortcut output parser](crates/openlogi-core/src/binding/key_combo.rs) and [binding shapes](crates/openlogi-core/src/binding/value.rs)
- [HID++ keyboard capture and reserved controls](crates/openlogi-device/src/session/keyboard.rs)
- [Keyboard session selection](crates/openlogi-agent-core/src/orchestrator.rs) and [device-action targeting](crates/openlogi-agent-core/src/dpi.rs)
- [Thumb-wheel and gesture dispatch](crates/openlogi-agent-core/src/watchers/gesture/dispatch.rs)
- [Keyboard GUI targets](crates/openlogi-desktop/src/features/keyboard/function_row/key_points.rs), [keyboard editor](crates/openlogi-desktop/src/features/keyboard/function_row.rs), and [mouse editor](crates/openlogi-desktop/src/features/mouse/inspector.rs)
- [macOS action backend](crates/openlogi-inject/src/inject/macos.rs), [Linux action backend](crates/openlogi-inject/src/inject/linux.rs), and [Windows action backend](crates/openlogi-inject/src/inject/windows.rs)
- [Actions Ring slot validation](crates/openlogi-core/src/binding/action_ring.rs)
