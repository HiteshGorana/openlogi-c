# OpenLogi Pure C (`openlogi-c`)

[![macOS CI](https://github.com/HiteshGorana/openlogi-c/actions/workflows/macos.yml/badge.svg)](https://github.com/HiteshGorana/openlogi-c/actions/workflows/macos.yml)

A lightweight native background driver and CLI tool for Logitech HID++ peripherals (MX Master 3S, MX Master 2S, MX Anywhere 3, and compatible devices), written in **Pure C (C99)**. The current hardware, input capture, and input synthesis backends target **macOS**.

---

## 1. Motivation & Benchmark Comparison

The standard OpenLogi Rust agent provides great device control, but requires a heavy runtime footprint. Rewriting the core agent and control tool in Pure C achieves dramatic performance and resource efficiency gains:

| Metric | Rust Agent (`openlogi-agent`) | Pure C Daemon (`openlogid`) | Improvement |
|---|---|---|---|
| **RAM Footprint (RSS)** | ~15.2 MB | **1.6 MB** | **~10x lighter** |
| **Stripped Binary Size** | ~12.4 MB | **352 KB** | **~35x smaller** |
| **Idle CPU Usage** | ~0.05% - 0.2% | **0.00%** | **Near zero** |
| **Build Dependencies** | 80+ Cargo crates | **Standard C library + macOS frameworks** (+ 2 vendored C libraries) | **No third-party package downloads** |
| **IPC Protocol** | Rust `tarpc` / `bincode` (binary) | **JSON-RPC 2.0** (human-readable) | **Universal & language-neutral** |
| **Build Time** | 45s - 90s | **< 1.5 seconds** | **~40x faster** |

These are reported baseline measurements; binary sizes and resource use depend on the compiler, architecture, and device configuration.

---

## 2. Architecture & Components

```
openlogi-c/
├── bin/                        # Compiled tools and test executables
│   ├── openlogid               # Background driver daemon
│   ├── openlogictl             # CLI tool & setup wizard
│   └── openlogi_discover       # Standalone hardware diagnostic tool
│
├── include/                    # C Public Headers
│   ├── hidpp.h                 # HID++ 1.0/2.0 packet framing and error decoding
│   ├── hidpp_reprog.h          # 0x1B04 ReprogControls diversion driver
│   ├── hid_transport.h         # Native OS HID abstraction interface
│   ├── gesture.h               # Swipe accumulator & gesture state machine
│   ├── inject.h                # OS input synthesis (CoreDock, media keys, shortcuts)
│   ├── config.h                # Schema v7 TOML config structures & resolver
│   ├── config_watch.h          # kqueue file watcher for config hot-reloading
│   ├── app_watch.h             # Active foreground app detection (NSWorkspace)
│   ├── single_instance.h       # POSIX flock daemon process exclusivity
│   ├── ipc_server.h            # JSON-RPC 2.0 UNIX domain socket server
│   └── ipc_client.h            # JSON-RPC 2.0 client implementation
│
├── src/                        # Implementations
│   ├── hid/                    # Packet engine, features (DPI, SmartShift, Battery, Diversion)
│   ├── core/                   # Signals, gestures, single-instance lock, config resolver
│   ├── hook/                   # OS input capture (CGEventTap on macOS)
│   ├── inject/                 # OS input injection (CoreDock, NSEvent media keys, CGEvent)
│   ├── ipc/                    # Non-blocking UNIX socket server & client
│   ├── platform/macos/         # IOKit HID transport & NSWorkspace watcher
│   ├── daemon/main.c           # Daemon lifecycle, signal handlers, main loop
│   └── cli/                    # CLI commands & interactive setup wizard
│
├── vendor/                     # Vendored Zero-Dependency Libraries (MIT)
│   ├── yyjson/                 # High-performance ANSI C JSON library
│   └── tomlc99/                # C99 TOML parser
│
├── packaging/                  # Service Definitions
│   ├── macos/                  # io.github.openlogi.agent.plist (launchd User Agent)
│   └── linux/                  # openlogid.service (systemd user unit)
│
├── .github/workflows/          # macOS build, tests, and sanitizer CI
└── tests/                      # 19 unit and regression suites / 97 test cases
```

---

## 3. Key Capabilities

### 3.1 Hardware Transport & Firmware Diversion
- **Logitech HID++ Protocol**: Supports Short (3-byte), Long (16-byte), and Very Long (60-byte) packets across HID++ 1.0 and HID++ 2.0+.
- **Feature Probing**: Auto-detects device name (`0x0005`), battery status & charging state (`0x1000`/`0x1004`), sensor DPI limits (`0x2201`), and SmartShift ratchet/free-spin scroll modes (`0x2110`).
- **Firmware ReprogControls Diversion (`0x1B04`)**: Diverts special mouse controls (thumb button, mode shift) into software events with high-rate raw XY pointer tracking.
- **Shutdown Cleanup**: Signal handlers (`SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`) request teardown of diverted controls when the daemon exits.

### 3.2 Gestures & Input Synthesis
- **Swipe Accumulator**: Deadzone filtering (40 units), axis cross-ratio gating (35%), 160 ms minimum hold time, and mid-swipe commit-once semantics prevent misfires.
- **Native macOS Actions**:
  - Mission Control (`com.apple.expose.awake`) via private `CoreDockSendNotification`.
  - App Exposé (`com.apple.expose.front.awake`), Show Desktop, and Launchpad.
  - Virtual Desktop / Spaces switching.
  - Media keys (Play/Pause, Next Track, Previous Track, Volume, Mute) via native `NSEvent` system events.
  - Arbitrary keyboard shortcuts and smooth sub-pixel scroll wheel synthesis.

### 3.3 Dynamic Profiles & Configuration
- **TOML Config**: Parses OpenLogi schema v7 (`~/.config/openlogi/config.toml`).
- **Per-App Fallback Profiles**: Detects the frontmost application (`NSWorkspace`) and resolves its button bindings with fallback to the device profile.
- **Hot-Reloading**: Kernel `kqueue` watcher triggers instant configuration reload on file change, handling atomic file writes.
- **Actions & Workflows**: 45 catalog actions and 8 parameterized/workflow action types, including custom shortcuts, held keys, text, application launch, AppleScript, and shell commands. See [ACTIONS.md](ACTIONS.md) or run `openlogictl actions` for the catalog.

### 3.4 Open JSON-RPC 2.0 IPC
- Operates over local UNIX domain socket `~/.local/state/openlogi/openlogi.sock`.
- Standard JSON-RPC 2.0 methods:
  - `openlogi.get_status`
  - `openlogi.get_inventory`
  - `openlogi.set_dpi`
  - `openlogi.set_smartshift`
  - `openlogi.reload_config`
  - `openlogi.subscribe` (stream real-time event notifications)

---

## 4. Building & Installation

### Prerequisites

- macOS with Xcode Command Line Tools (`clang` and `make`). Install them with `xcode-select --install` if needed.
- The build links the system `IOKit`, `CoreFoundation`, `ApplicationServices`, and `AppKit` frameworks. JSON and TOML libraries are vendored in this repository.

Linux service packaging is included, but Linux hardware and input backends are not implemented in this tree.

### Quick Start

```bash
git clone https://github.com/HiteshGorana/openlogi-c.git
cd openlogi-c
make all

# Start in the foreground to inspect startup messages
./bin/openlogid --verbose
```

In another terminal, from the repository directory:

```bash
./bin/openlogictl status
./bin/openlogictl list
./bin/openlogictl setup
```

Grant the daemon **Accessibility** and **Input Monitoring** access in **System Settings → Privacy & Security** for input capture and synthesis, then restart it. Use `./bin/openlogid --daemon` to run it in the background, or enable the login service described below.

### Commands

```bash
# Build all production binaries and test executables
make all

# Build only the three runtime tools
make tools

# Run all 19 suites (97 test cases)
make test

# Show individual test cases and timing
make test VERBOSE=1

# Run AddressSanitizer & UndefinedBehaviorSanitizer verification
make asan

# Strip release symbols and display binary sizes
make strip

# Install to a user-writable prefix
make install PREFIX="$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"

# Or install system-wide to /usr/local/bin
sudo make install PREFIX=/usr/local

# Uninstall from the same prefix used for installation
make uninstall PREFIX="$HOME/.local"
```

---

## 5. CLI Reference (`openlogictl`)

`openlogictl` is the pure C command-line tool for controlling devices, inspecting the daemon, and managing settings.

### Common Commands

```bash
# Check daemon health, permissions, and active app
openlogictl status

# Output in JSON format (ideal for menu bar widgets, Raycast, and scripts)
openlogictl --json status

# List all connected Logitech devices, battery %, and current settings
openlogictl list

# Set DPI resolution in real time
openlogictl dpi set 2400

# Configure SmartShift scroll wheel mode and flick threshold
openlogictl smartshift set ratchet 18
openlogictl smartshift set freespin 25
openlogictl smartshift set auto 14

# Force an immediate reload of config.toml
openlogictl reload

# Stream real-time intercepted button events and gestures
openlogictl monitor

# Launch the interactive ANSI setup wizard
openlogictl setup

# Inspect available actions and configuration
openlogictl actions
openlogictl options
openlogictl config path
openlogictl config show

# Autostart management (launchd on macOS)
openlogictl autostart status
openlogictl autostart enable
openlogictl autostart disable
```

### Global Flags

- `--json`: Machine-readable JSON output for all subcommands.
- `-s, --socket <path>`: Specify custom UNIX domain socket path.
- `-a, --autospawn`: Automatically launch `openlogid --daemon` if not already running.
- `-h, --help`: Display command usage.

---

## 6. Configuration

The default file is `~/.config/openlogi/config.toml`, or `$XDG_CONFIG_HOME/openlogi/config.toml` when set. Use `openlogictl setup` to configure it interactively, or edit it directly. For example:

```toml
schema_version = 7

[devices.default]
dpi = 1600
dpi_presets = [800, 1600, 3200]

[devices.default.smartshift]
mode = "ratchet"
auto_disengage = 16

[devices.default.bindings]
Back = "BrowserBack"
Forward = "BrowserForward"
MiddleClick = { CustomShortcut = "Cmd+Shift+T" }

[devices.default.bindings.GestureButton]
Click = "MissionControl"
Up = "MissionControl"
Down = "AppExpose"
Left = "PreviousDesktop"
Right = "NextDesktop"
```

For a specific mouse, replace `default` with a quoted device key from `openlogictl list`, such as `[devices."device:YOUR_SERIAL"]`, and use that key in the remaining tables. Changes are watched automatically; `openlogictl reload` also reloads the file on demand.

---

## 7. Autostart & Service Management

To have `openlogid` start automatically when you log in on macOS, run these commands from the repository directory after building:

```bash
./bin/openlogictl autostart enable

# Verify status
./bin/openlogictl autostart status

# Stop the service and remove its login entry
./bin/openlogictl autostart disable
```

The CLI generates `~/Library/LaunchAgents/io.github.openlogi.agent.plist` and loads it with `launchctl`. It uses `/usr/local/bin/openlogid` when present, otherwise the built `bin/openlogid` in the current repository. Keep the selected binary at that path. Service logs are written to `/tmp/openlogid.stdout.log` and `/tmp/openlogid.stderr.log`.

---

## 8. Tests & Continuous Integration

The suite contains **97 test cases across 19 suites**, covering HID++ framing and feature probing, diversion, gestures, configuration, IPC, CLI commands, single-instance locking, button timing, scrolling, wake/reconnect behavior, DPI cycling, session recovery, and actions/workflows.

```bash
make test
make asan
```

The [macOS CI workflow](.github/workflows/macos.yml) runs on pushes, pull requests, and manual dispatches. It uses GitHub's [`macos-latest` runner](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) and Clang to build all three tools and the test executables, run the suite, and rebuild/run it with AddressSanitizer and UndefinedBehaviorSanitizer. Hardware control and macOS permission behavior require verification on a Mac with a connected device.

See [AGENTS.md](AGENTS.md) for the architecture, test suite map, and development conventions.
