# OpenLogi C — Complete Usage & Command Reference

This document is the comprehensive reference guide for `openlogi` and `OpenLogi.app`. It covers every command, flag, configuration option, hardware modification, gesture mapping, and automation workflow.

---

## Table of Contents

1. [Quick Start & Running Modes](#1-quick-start--running-modes)
   - [Zero-Touch App Mode (`OpenLogi.app` / `.dmg`)](#zero-touch-app-mode-openlogiapp--dmg)
   - [CLI & Service Mode](#cli--service-mode)
2. [CLI Command Reference](#2-cli-command-reference)
   - [Global Options & Flags](#global-options--flags)
   - [Diagnostics & Permissions (`doctor`, `permissions`, `status`)](#diagnostics--permissions)
   - [Hardware Adjustments (`dpi`, `smartshift`, `scroll`)](#hardware-adjustments)
   - [Live Inspection & Discovery (`list`, `monitor`, `discover`, `setup`)](#live-inspection--discovery)
   - [Configuration Management (`config`, `reload`, `options`)](#configuration-management)
   - [Service & Process Management (`service`, `restart`, `stop`, `uninstall`)](#service--process-management)
3. [Configuration Guide (`config.toml`)](#3-configuration-guide-configtoml)
   - [File Locations](#file-locations)
   - [Complete Reference Template](#complete-reference-template)
   - [Scroll & Wheel Customization](#scroll--wheel-customization)
   - [Hardware Settings: Sensor DPI & SmartShift](#hardware-settings-sensor-dpi--smartshift)
   - [Button Bindings & Names](#button-bindings--names)
   - [Thumb Gestures (4-Way Directional Engine)](#thumb-gestures-4-way-directional-engine)
   - [Per-Application Profiles](#per-application-profiles)
   - [Parameterized Actions & Workflows](#parameterized-actions--workflows)
4. [Complete Action Catalog (All 53 Actions)](#4-complete-action-catalog-all-53-actions)
5. [Troubleshooting & FAQ](#5-troubleshooting--faq)

---

## 1. Quick Start & Running Modes

OpenLogi can run in two ways: as a **zero-terminal macOS application**, or as a **high-performance CLI / background daemon**.

### Zero-Touch App Mode (`OpenLogi.app` / `.dmg`)
1. Download **`OpenLogi-arm64.dmg`** (or `OpenLogi-x86_64.dmg` for Intel Macs) from the [Releases page](https://github.com/HiteshGorana/openlogi-c/releases).
2. Open the disk image and drag **`OpenLogi.app`** into `/Applications`.
3. Double-click **`OpenLogi.app`**:
   - **First Launch**: OpenLogi guides you through **Input Monitoring**, then **Accessibility**. Enable OpenLogi in each pane, then return and click **Check Again**. If macOS asks to **Quit & Reopen**, accept; setup resumes. Use **More Options → Reveal OpenLogi** if the app is missing from the permission list.
   - **Verification**: Setup checks the background driver's permissions, connected mouse, and active input hook before offering **Start at Login** or **Start Manually**. Manual startup lasts for the current login session and does not install a login item.
   - **Finish Later**: Progress is saved. Opening the app again resumes unfinished setup even when the driver is already running.
   - **Subsequent Launches (While Running)**: OpenLogi displays driver readiness, with **Keep Running**, **Stop OpenLogi**, and **More Options** for setup or uninstall. Missing driver permissions return you to setup.

Ad-hoc builds may need permission approval again after an update or rebuild. If
the toggles are enabled but access remains denied, remove the old entries and add
the installed copy again. Always grant access to the copy in Applications.

### CLI & Service Mode
If you prefer running via Terminal or Homebrew prefix (`~/.local/bin`):
```sh
# Install or update via curl
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh

# Enable background launchd login service
openlogi service enable

# Check status and connected hardware
openlogi status
openlogi list
```

---

## 2. CLI Command Reference

All CLI commands follow the pattern:
```sh
openlogi [global options] <command> [subcommand] [arguments]
```

### Global Options & Flags

Place flags **before** the subcommand:

| Flag | Description | Example |
|---|---|---|
| `--json`, `-j` | Machine-readable JSON output for scripting | `openlogi --json status` |
| `--socket`, `-s <path>` | Custom UNIX domain socket path | `openlogi -s /tmp/my.sock list` |
| `--autospawn`, `-a` | Auto-launch daemon if not currently running | `openlogi -a dpi get` |
| `--version`, `-v` | Print OpenLogi C version and exit | `openlogi --version` |
| `--help`, `-h` | Display help screen | `openlogi --help` |

---

### Diagnostics & Permissions

#### `openlogi doctor`
Runs a full 5-stage health check inspecting binary integrity, code signatures, Accessibility and Input Monitoring permissions, running daemon state, and connected Logitech hardware:
```sh
openlogi doctor
```
```
[1/5] Binary & Environment:
    CLI Executable:   ~/.local/bin/openlogi
    macOS App Bundle: /Applications/OpenLogi.app
    Code Signature:   VALID (Signed)
[2/5] macOS System Permissions:
    Accessibility:    GRANTED (Event Tap Active)
    Input Monitoring: GRANTED (Raw HID++ Protocol Active)
[3/5] Daemon & IPC Server:
    Daemon Health:    HEALTHY (PID 26825, Uptime 12m)
[4/5] Hardware Discovery:
    Logitech Devices: 1 connected
    Device 0:         MX Master 2S (Battery: 85%, Online)
[5/5] Configuration:
    Active File:      ~/.config/openlogi/config.toml
```

#### `openlogi permissions [request | reveal | reset]`
Inspects or manages macOS privacy permissions:
```sh
openlogi permissions          # Inspect status of Accessibility & Input Monitoring
openlogi permissions request  # Trigger macOS system permission dialogs
openlogi permissions reveal   # Open System Settings directly to Privacy panes
openlogi permissions reset    # Reset system permissions via tccutil (for testing)
```

#### `openlogi status`
Shows background daemon health, PID, uptime, active frontmost application, and device counts:
```sh
openlogi status
openlogi --json status
```

---

### Hardware Adjustments

#### `openlogi scroll`
Inspects or adjusts mouse wheel direction, smooth scrolling, and resolution:
```sh
openlogi scroll                     # View current scroll settings
openlogi scroll invert toggle       # Toggle wheel scroll inversion
openlogi scroll invert true         # Enable wheel inversion (natural wheel, trackpad unaffected)
openlogi scroll invert false        # Disable wheel inversion (standard direction)
openlogi scroll resolution high     # Set continuous high-resolution wheel ticks
openlogi scroll resolution low      # Set discrete notch-per-step ratchet scrolling
```

#### `openlogi dpi [get | set <value>]`
Queries or sets sensor DPI dynamically in real time:
```sh
openlogi dpi                        # Show current DPI of primary mouse
openlogi dpi set 1600               # Set DPI to 1600 on primary mouse
openlogi dpi set 3200 "device:0"    # Set DPI on specific device key
```

#### `openlogi smartshift [get | set <mode> <threshold>]`
Configures the motorized SmartShift ratchet/free-spin wheel:
```sh
openlogi smartshift                        # Show SmartShift state
openlogi smartshift set ratchet 16         # Ratchet mode with sensitivity threshold 16
openlogi smartshift set freespin 0         # Smooth frictionless free-spin mode
```

---

### Live Inspection & Discovery

#### `openlogi list` (aliases: `devices`, `inventory`)
Lists all connected Logitech hardware nodes, battery levels, sensor DPI, and SmartShift state:
```sh
openlogi list
openlogi --json list
```

#### `openlogi monitor`
Streams live real-time intercepted button clicks, directional gesture swipes, and application switches:
```sh
openlogi monitor
```
```
[Event] ButtonDown: CID=0x00C3 (GestureButton)
[Gesture] SwipeInProgress: deltaX=-42, deltaY=12 (Swipe Left)
[Gesture] Committed: PreviousDesktop
[Event] ButtonUp: CID=0x00C3 (GestureButton)
```

#### `openlogi discover` (alias: `scan`)
Scans raw macOS IOKit USB and Bluetooth endpoints to inspect Logitech vendor registers (`0x046D`), receiver pairing slots, and supported HID++ feature tables:
```sh
openlogi discover
```

#### `openlogi setup`
Opens an interactive ANSI terminal menu to adjust DPI, test SmartShift modes, toggle scroll inversion, and reload configurations interactively:
```sh
openlogi setup
```

---

### Configuration Management

#### `openlogi config [path | show | get <key> | set <key> <val>]`
Inspects and modifies `~/.config/openlogi/config.toml`:
```sh
openlogi config path                       # Print path to active config.toml
openlogi config show                       # Print contents of config.toml
openlogi config get invert_scroll          # Read value of invert_scroll
openlogi config set invert_scroll true     # Atomically update invert_scroll = true
```

#### `openlogi reload`
Triggers an immediate transactional reload of `config.toml` without restarting the daemon:
```sh
openlogi reload
```

#### `openlogi actions [filter]`
Lists all supported button actions, system hotkeys, and shortcuts:
```sh
openlogi actions
openlogi actions media       # Filter catalog by "media"
openlogi actions window      # Filter catalog by "window"
```

---

### Service & Process Management

#### `openlogi service [status | enable | disable | start | stop | restart]`
Manages the macOS `launchd` user agent (`io.github.openlogi.agent`):
```sh
openlogi service status      # Check if launchd service is enabled and running
openlogi service enable      # Register and launch daemon on system login
openlogi service disable     # Unregister launchd agent and stop autostart
openlogi service restart     # Restart the background service
```

#### `openlogi stop`
Stops any running daemon instance cleanly, safely undiverting mouse hardware:
```sh
openlogi stop
```

#### `openlogi restart`
Restarts the active background daemon:
```sh
openlogi restart
```

#### `openlogi uninstall [--dry-run]`
Performs a full clean uninstall:
```sh
openlogi uninstall --dry-run  # Preview removal without touching files
openlogi uninstall            # Unload service, stop daemon, and remove installed tools
```

---

## 3. Configuration Guide (`config.toml`)

### File Locations
OpenLogi loads configuration from the first available location:
1. Custom flag: `openlogi daemon --config /path/to/custom.toml`
2. `$XDG_CONFIG_HOME/openlogi/config.toml` (if `$XDG_CONFIG_HOME` is set)
3. `~/.config/openlogi/config.toml` (default)

Config reloads are **transactional**: edits are monitored via kernel `kqueue`. If you save a file with a syntax error or invalid TOML, OpenLogi logs the error and safely continues running your previous valid configuration without dropping inputs or leaking memory.

---

### Complete Reference Template

Save this template at `~/.config/openlogi/config.toml`:

```toml
schema_version = 7

# ==============================================================================
# GLOBAL & DEFAULT DEVICE PROFILE
# ==============================================================================
[devices.default]

# ------------------------------------------------------------------------------
# 1. MOUSE WHEEL & SCROLL SETTINGS
# ------------------------------------------------------------------------------
# Inverts mouse wheel scrolling to standard "natural" style while strictly
# preserving macOS trackpad momentum and swipe direction.
invert_scroll = true

# Scroll resolution: "high" (smooth sub-pixel ticks) or "low" (discrete ratchet steps)
scroll_resolution = "high"

# Enable smooth sub-pixel scroll wheel interpolation
smooth_scroll = true

# Scroll sensitivity multipliers (1–50, default: 14)
vertical_scroll_sensitivity = 14
thumbwheel_sensitivity = 14

# ------------------------------------------------------------------------------
# 2. SENSOR DPI & PRESETS
# ------------------------------------------------------------------------------
# Default sensor DPI applied on connect and system wake
dpi = 1600

# Presets cycled through when invoking the "CycleDpi" action
dpi_presets = [800, 1200, 1600, 2400, 3200]

# ------------------------------------------------------------------------------
# 3. SMARTSHIFT WHEEL (MX Master / MX Anywhere)
# ------------------------------------------------------------------------------
[devices.default.smartshift]
# Mode: "ratchet" (clicky steps), "freespin" (frictionless), or "auto"
mode = "ratchet"

# Speed threshold to auto-switch from ratchet to free-spin (1–255, default: 16)
threshold = 16

# Automatically switch back to ratchet when wheel slows down
auto_disengage = true

# ------------------------------------------------------------------------------
# 4. DEFAULT BUTTON BINDINGS
# ------------------------------------------------------------------------------
[devices.default.bindings]
# Side navigation buttons
Back = "BrowserBack"
Forward = "BrowserForward"

# Middle click (scroll wheel click)
MiddleClick = { CustomShortcut = "Cmd+Shift+T" }

# Top mode button behind scroll wheel
DpiToggle = "ToggleSmartShift"

# Horizontal thumbwheel (MX Master series)
ThumbWheelUp = "ScrollLeft"
ThumbWheelDown = "ScrollRight"

# ------------------------------------------------------------------------------
# 5. THUMB GESTURE BUTTON (4-WAY CARDINAL ENGINE)
# ------------------------------------------------------------------------------
[devices.default.bindings.GestureButton]
# Single click without mouse movement
Click = "MissionControl"

# Hold gesture button + move mouse Up
Up = "MissionControl"

# Hold gesture button + move mouse Down
Down = "AppExpose"

# Hold gesture button + move mouse Left
Left = "PrevSpace"

# Hold gesture button + move mouse Right
Right = "NextSpace"

# ------------------------------------------------------------------------------
# 6. PER-APPLICATION PROFILES
# ------------------------------------------------------------------------------
# Override bindings when Safari is the active frontmost application
[devices.default.per_app_bindings."com.apple.Safari"]
Back = "BrowserBack"
Forward = "BrowserForward"
MiddleClick = { CustomShortcut = "Cmd+W" }

# Override bindings when Xcode / Zed / VS Code is frontmost
[devices.default.per_app_bindings."dev.zed.Zed"]
Back = { CustomShortcut = "Cmd+[" }
Forward = { CustomShortcut = "Cmd+]" }
MiddleClick = { CustomShortcut = "Cmd+P" }

[devices.default.per_app_bindings."dev.zed.Zed".GestureButton]
Click = { CustomShortcut = "Cmd+Shift+F" }
Left = { CustomShortcut = "Cmd+Alt+Left" }
Right = { CustomShortcut = "Cmd+Alt+Right" }
```

---

### Button Bindings & Names

OpenLogi recognizes all standard Logitech button identifier names (case-insensitive):

| Button Identifier | Physical Hardware Control | Typical Default |
|---|---|---|
| `LeftClick` | Main left mouse button | Left Click |
| `RightClick` | Main right mouse button | Right Click |
| `MiddleClick` | Wheel depress / button 3 | Middle Click |
| `Back` | Rear thumb button (Button 4) | Browser Back |
| `Forward` | Front thumb button (Button 5) | Browser Forward |
| `GestureButton` | Thumb-rest tactile switch (CID `0x00C3`) | Gestures |
| `DpiToggle` | Top mode button behind wheel (CID `0x00C4`) | SmartShift / DPI |
| `ThumbWheelUp` | Horizontal thumbwheel rolled forward/up | Volume / Horizontal Scroll |
| `ThumbWheelDown` | Horizontal thumbwheel rolled backward/down | Volume / Horizontal Scroll |

---

### Thumb Gestures (4-Way Directional Engine)

When you assign actions to `[devices.default.bindings.GestureButton]`, OpenLogi activates a sub-pixel swipe accumulator:
1. Depress and hold the thumb button.
2. Nudge the mouse in any cardinal direction (`Up`, `Down`, `Left`, `Right`).
3. As soon as the pointer displacement exceeds the sensitivity threshold, OpenLogi commits the corresponding direction action and discards the click.
4. If you press and release the button without moving the mouse, the `Click` action triggers instead.

---

### Parameterized Actions & Workflows

Beyond simple action names, buttons can execute complex automation:

#### 1. Custom Keyboard Shortcut (`CustomShortcut`)
Emits chords with modifiers (`Cmd`, `Ctrl`, `Alt`/`Option`, `Shift`):
```toml
MiddleClick = { CustomShortcut = "Cmd+Shift+4" }       # Screen capture region
Back = { CustomShortcut = "Ctrl+Cmd+F" }              # Toggle fullscreen
Forward = { CustomShortcut = "Cmd+Alt+I" }            # Developer tools
```

#### 2. Open Application (`OpenApplication`)
Launches applications by bundle ID or filesystem path:
```toml
DpiToggle = { OpenApplication = "/Applications/Terminal.app" }
Back = { OpenApplication = "com.apple.Music" }
```

#### 3. Inject Text (`TypeText`)
Types text strings automatically:
```toml
MiddleClick = { TypeText = "git status\n" }
```

#### 4. Run AppleScript (`RunAppleScript`)
Executes native AppleScript scripts asynchronously:
```toml
Back = { RunAppleScript = 'tell application "Spotify" to next track' }
```

#### 5. Run Shell Command (`RunShellCommand`)
Executes any shell pipeline in the background:
```toml
Forward = { RunShellCommand = "pmset sleepnow" }
```

#### 6. Multi-Step Automation (`Workflow`)
Chains multiple actions with millisecond-precision delays:
```toml
DpiToggle = { Workflow = [
    { PressKey = "Cmd+Space" },
    { Delay = 120 },
    { TypeText = "Terminal" },
    { Delay = 50 },
    { PressKey = "Return" }
]}
```

---

## 4. Complete Action Catalog (All 53 Actions)

OpenLogi C implements all 53 actions natively without spawning heavy scripting engines:

### Navigation & Desktop Spaces
- `MissionControl`: Open macOS Mission Control (Spaces overview)
- `AppExpose`: Show all open windows for the current active app
- `ShowDesktop`: Hide all windows and show the Desktop
- `Launchpad`: Toggle macOS Launchpad grid
- `NextSpace` / `NextDesktop`: Slide to the desktop space on the right (`Ctrl+Right`)
- `PrevSpace` / `PreviousDesktop`: Slide to the desktop space on the left (`Ctrl+Left`)

### System & Power
- `LockScreen`: Instantly lock the macOS session (`Cmd+Ctrl+Q`)
- `Sleep`: Put the Mac to sleep immediately
- `Screenshot`: Capture full screen image (`Cmd+Shift+3`)
- `CaptureRegion`: Trigger interactive crosshair region capture (`Cmd+Shift+4`)

### Window & Application Management
- `AppSwitchNext`: Switch to next application (`Cmd+Tab`)
- `AppSwitchPrev`: Switch to previous application (`Cmd+Shift+Tab`)
- `CloseWindow`: Close active window (`Cmd+W`)
- `MinimizeWindow`: Minimize active window (`Cmd+M`)
- `ToggleFullscreen`: Toggle native macOS fullscreen (`Ctrl+Cmd+F`)

### Media & Volume Controls
- `MediaPlayPause`: Play / pause current media (`NX_KEYTYPE_PLAY`)
- `MediaNextTrack`: Skip to next audio/video track (`NX_KEYTYPE_NEXT`)
- `MediaPrevTrack`: Skip to previous track (`NX_KEYTYPE_PREVIOUS`)
- `VolumeUp`: Increase audio volume (`NX_KEYTYPE_SOUND_UP`)
- `VolumeDown`: Decrease audio volume (`NX_KEYTYPE_SOUND_DOWN`)
- `VolumeMute`: Toggle audio mute (`NX_KEYTYPE_MUTE`)

### Browser & Document Navigation
- `BrowserBack`: Navigate to previous page (`Cmd+[` or historical back)
- `BrowserForward`: Navigate forward (`Cmd+]`)
- `BrowserRefresh`: Reload current webpage (`Cmd+R`)
- `BrowserNewTab`: Open a new tab (`Cmd+T`)
- `BrowserCloseTab`: Close current tab (`Cmd+W`)
- `BrowserReopenClosedTab`: Restore last closed tab (`Cmd+Shift+T`)

### Editing & Clipboard
- `Undo`: Undo last action (`Cmd+Z`)
- `Redo`: Redo last action (`Cmd+Shift+Z`)
- `Cut`: Cut selection to clipboard (`Cmd+X`)
- `Copy`: Copy selection to clipboard (`Cmd+C`)
- `Paste`: Paste clipboard contents (`Cmd+V`)
- `SelectAll`: Select all content (`Cmd+A`)
- `Find`: Open search dialog (`Cmd+F`)

### Mouse & Pointer Actions
- `LeftClick`, `RightClick`, `MiddleClick`: Standard mouse button synthesis
- `DoubleClick`, `TripleClick`: Multi-click pointer events
- `ScrollUp`, `ScrollDown`, `ScrollLeft`, `ScrollRight`: Synthetic wheel scrolling

### Hardware & Mode Cycling
- `CycleDpi`: Cycle through `dpi_presets` configured in `config.toml`
- `ToggleSmartShift`: Switch ratchet wheel between ratchet and free-spin

---

## 5. Troubleshooting & FAQ

### Mouse buttons stopped working or feel unresponsive
1. Run `openlogi doctor` in Terminal.
2. Ensure both **Accessibility** and **Input Monitoring** show **GRANTED**.
3. If Input Monitoring is missing, macOS firmware security blocks sending the `0x1B04` command to divert buttons. Run `openlogi permissions reveal` to toggle OpenLogi ON.

### How do I uninstall OpenLogi completely?
- **From GUI**: Double-click `/Applications/OpenLogi.app` while it is running and click **Uninstall**.
- **From CLI**: Run `openlogi uninstall`.
- **From Source**: Run `make uninstall`.

### Why does trackpad scrolling feel fine while mouse wheel is inverted?
OpenLogi inspects the `kCGScrollWheelEventIsContinuous` and momentum phase bitflags on incoming events. Synthetic inversion (`HOOK_ACTION_INVERT`) is exclusively applied to discrete ratcheted mouse wheels; smooth Apple trackpad swipes are passed through untouched.

### How much memory does OpenLogi consume?
OpenLogi is written in clean, standard C99 with zero external runtime libraries:
- **RAM**: ~1.6 MB – 2.0 MB resident memory (compared to ~15 MB for Rust or ~60 MB for Electron).
- **CPU**: **0.00%** at idle (purely event-driven via Mach ports and CFRunLoop).
- **Binary Size**: ~450 KB stripped.
