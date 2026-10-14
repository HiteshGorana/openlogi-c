# OpenLogi C

[![macOS CI](https://github.com/HiteshGorana/openlogi-c/actions/workflows/macos.yml/badge.svg)](https://github.com/HiteshGorana/openlogi-c/actions/workflows/macos.yml)

A small, native macOS daemon and command-line tool for customizing Logitech HID++ mice. Written in C99, OpenLogi combines device discovery, DPI and SmartShift control, mouse-button bindings, gestures, and local JSON-RPC in one executable.

**Development status:** beta. Core reliability, transactional config reloading, asynchronous action queues, reconnect handling, and mouse-wheel inversion are implemented and verified. Start with one mouse and review the [current limitations](#current-limitations). The [production readiness plan](improvements.md) tracks the completed fixes and release criteria.

[Get started](#get-started) · [Commands](#commands) · [Configuration](#configuration) · [Troubleshooting](#troubleshooting) · [Development](#development)

## What is here today

- **Native device access:** IOKit transport and HID++ feature probing for device information, battery, sensor DPI, SmartShift, and programmable controls.
- **Mouse customization:** bindings for captured buttons, thumb-button gestures, per-application overrides, shortcuts, and system/media actions.
- **A local control interface:** CLI commands, an interactive hardware setup menu, and a UNIX socket for integrations.
- **A compact runtime:** one executable with compatibility aliases, system frameworks, and vendored JSON/TOML parsers. Building requires no third-party package downloads.
- **Reliable background operation:** transactional config hot-reloading with zero memory leaks, non-blocking asynchronous action queues, hardware setting persistence across wake/reconnect, and truthful macOS permission verification.

Features depend on the device, firmware, connection, and macOS permissions. Discovery alone does not establish full compatibility.

| Area | Current scope |
| --- | --- |
| Operating system | macOS backend; prebuilt releases target macOS 13 or newer. |
| Architectures | Separate Apple Silicon (`arm64`) and Intel (`x86_64`) release archives. |
| Devices | Logitech HID++ mice, including the MX Master and MX Anywhere families as intended targets. Model/transport certification remains part of the roadmap. |
| Receivers | The diagnostic scanner probes paired receiver slots; the daemon's receiver-slot support is incomplete. |
| Keyboard | Keyboard shortcuts can be emitted by mouse bindings. Per-keyboard input remapping is planned. |
| Interface | CLI and terminal setup menu. A graphical configuration app is planned. |
| Linux / Windows | No implemented hardware/input backend in this tree. Linux service packaging is included for future work. |

## Get started

### 1. Install

Run as your normal macOS user:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

The installer downloads the latest published release, checks its SHA-256 checksum, and installs to `~/.local/bin`. It does not require Xcode, a compiler, or `sudo`.

Add the installation directory to your shell's `PATH`, and save the same line in your shell profile if needed:

```sh
export PATH="$HOME/.local/bin:$PATH"
openlogi --version
```

Alternatively, use `~/.local/bin/openlogi` directly. Archives are also available on the [releases page](https://github.com/HiteshGorana/openlogi-c/releases).

### 2. Grant permissions

In **System Settings → Privacy & Security**, grant `openlogi` both **Accessibility** and **Input Monitoring** access. Use the **+** button and **Cmd+Shift+G** to select `~/.local/bin/openlogi`.

Grant access to the executable you will actually run. A source build or custom-prefix installation has a different path. Current releases use ad-hoc signing, so macOS may require permissions again after an update.

### 3. Create your bindings

Create the configuration directory:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/openlogi"
openlogi config path
```

Save the [starter configuration](#starter-configuration) at the path printed by the second command before starting the daemon. For an existing installation, back up your configuration before editing it.

### 4. Start and inspect

```sh
openlogi service enable
openlogi status
openlogi list
```

Then open the interactive menu if you want to adjust live DPI or SmartShift settings:

```sh
openlogi setup
```

`setup` provides device inspection, hardware adjustments, reload, and event monitoring. Button bindings are edited in TOML; the menu does not create a persistent binding configuration. See [troubleshooting](#troubleshooting) if the daemon starts but input or device control does not work.

## Commands

Most control commands require a running daemon. Help, version, action listing, and local configuration inspection can run independently.

| Task | Command |
| --- | --- |
| Show version | `openlogi --version` |
| Inspect daemon status | `openlogi status` |
| List device keys and reported settings | `openlogi list` |
| Read status as JSON | `openlogi --json status` |
| Request a DPI change | `openlogi dpi set 1600` |
| Target a specific device | `openlogi dpi set 1600 'device:YOUR_SERIAL'` |
| Request ratchet mode | `openlogi smartshift set ratchet 16` |
| Request free-spin mode | `openlogi smartshift set freespin 16` |
| Open the hardware setup menu | `openlogi setup` |
| List the action catalog | `openlogi actions` |
| Print the configuration path / file | `openlogi config path` / `openlogi config show` |
| Request configuration reload | `openlogi reload` |
| Subscribe to daemon events | `openlogi monitor` |
| Inspect the login service | `openlogi service status` |
| Start at login / stop the service | `openlogi service enable` / `openlogi service disable` |

Use a device key from `openlogi list` when targeting a device; its sensor determines the actual supported DPI values. Hardware commands change live settings, while `config show` prints the file on disk. Neither is a guarantee that the daemon has applied a persisted setting; see [current limitations](#current-limitations).

Place control flags **before** the command:

```sh
openlogi --json list
openlogi --socket /path/to/openlogi.sock status
```

JSON output is available for supported control commands; interactive setup and some config operations still emit text. `--autospawn` can start a daemon when connecting, but the login service is the simpler way to manage a regular installation.

For a temporary foreground session, first stop any existing login service, then run:

```sh
openlogi service disable
openlogi daemon --verbose
```

Press **Ctrl+C** to request shutdown. Run `openlogi service enable` afterward to restore login startup.

The compatibility aliases remain available: `openlogid` runs the daemon, `openlogictl` runs control commands, and `openlogi_discover` runs hardware diagnostics.

## Configuration

The default file is `~/.config/openlogi/config.toml`. If `XDG_CONFIG_HOME` is set, the path is `$XDG_CONFIG_HOME/openlogi/config.toml`. Run `openlogi config path` to see the CLI's path.

A manually started daemon can use another file with `openlogi daemon --config /path/to/config.toml`. Keep the CLI and daemon paths/environment aligned: a login service does not automatically inherit variables from your shell profile.

### Starter configuration

This example uses one default mouse profile. It maps the side buttons to browser navigation, the middle button to reopen a tab, and the mode button to toggle SmartShift where available.

```toml
schema_version = 7

[devices.default]
invert_scroll = false

[devices.default.bindings]
Back = "BrowserBack"
Forward = "BrowserForward"
MiddleClick = { CustomShortcut = "Cmd+Shift+T" }
DpiToggle = "ToggleSmartShift"

[devices.default.bindings.GestureButton]
Click = "MissionControl"
Up = "MissionControl"
Down = "AppExpose"
Left = "PreviousDesktop"
Right = "NextDesktop"

[devices.default.per_app_bindings."com.apple.Safari"]
MiddleClick = { CustomShortcut = "Cmd+T" }
```

Hold the gesture button and move the mouse to choose a direction; release without a committed swipe to run `Click`. A per-app binding overrides the default for that button. Unspecified buttons in an app profile fall back to the default profile.

The watcher reloads edits automatically, and `openlogi reload` requests an explicit reload. Config reloads are transactional: invalid TOML or parse errors are rejected without modifying or wiping the active in-memory configuration.

### Settings and action formats

| Location / setting | Syntax | Current meaning |
| --- | --- | --- |
| Root schema | `schema_version = 7` | The configuration format used by this implementation. |
| Invert scrolling | `invert_scroll = true` under `[devices.default]` | Reverses mouse wheel scroll direction while preserving trackpad natural scrolling. |
| Device presets | `dpi_presets = [800, 1600, 2400]` under `[devices.default]` | Presets used by `CycleDpiPresets` and `SetDpiPreset`. |
| Catalog action | `Back = "Copy"` in a bindings table | Invoke a named action; names are case-insensitive. |
| Shortcut | `MiddleClick = { CustomShortcut = "Cmd+Shift+T" }` | Emit a key combination. |
| Cycle DPI presets | `DpiToggle = "CycleDpiPresets"` | Advance through configured presets; per-device routing is incomplete. |
| Select a preset | `DpiToggle = { SetDpiPreset = 1 }` | Select the **zero-based preset index**, so `1` selects the second preset. This is not an exact DPI value. |
| Device hardware fields | `dpi`, `smartshift.mode`, `smartshift.auto_disengage` | Persisted device preferences, automatically reapplied upon device connect and system wake. |

When adding `[devices.default]`, place it before its nested tables in the example. Other recognized formats include `TypeText`, `OpenApplication`, `RunShellCommand`, `RunAppleScript`, and `Workflow`; their runtime restrictions are listed below.

`openlogi actions` exposes the catalog, but catalog presence does not prove a complete runtime implementation. [ACTIONS.md](ACTIONS.md) also describes behavior inherited from the broader Rust project, including keyboard and UI features that this C implementation does not yet provide.

## Current limitations

The [improvements roadmap](improvements.md) tracks the multi-phase engineering milestones. P0 core driver reliability (reconnects, transactional config safety, non-blocking action queues, truthful permissions, loop prevention, selective button diversion, and mouse wheel inversion) is fully resolved. The following items remain in active development:

| Area | What to expect today |
| --- | --- |
| Paired receiver slots | Multi-device enumeration and routing across shared Unifying/Bolt receiver slots is in development; single device per transport or direct BLE is recommended. |
| Held shortcuts | `HoldShortcut` is parsed but emitted as a momentary key tap rather than a sustained modifier hold. |
| Action ring overlay | `ShowActionsRing` has no on-screen visual overlay implementation yet. |
| Text injection | `TypeText` maps standard ASCII keycodes; non-ASCII UTF-8 sequences currently require clipboard paste. |
| Keyboard remapping | Per-keyboard hardware remapping is planned for a future release (mouse devices only today). |
| Linux / Windows | macOS backend only; Linux `hidraw`/`evdev` implementation is planned. |

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `openlogi: command not found` | Use `~/.local/bin/openlogi`, or add that directory to `PATH`. For custom installs, use the selected prefix. |
| Cannot connect to daemon | Run `openlogi service status`, then `openlogi service enable`. Check that CLI and daemon use the same socket path. |
| Permission shown as granted, but input does not work | Check both Accessibility and Input Monitoring for the actual executable in System Settings, then restart the service. |
| Another instance is already running | Stop the login service before starting a foreground daemon. Stop a foreground session with Ctrl+C before enabling the service. |
| Device disappears after sleep/reconnect | Disconnect/reconnect and sleep/wake recovery is automatic. If the device does not appear, power-cycle the mouse or check Bluetooth connection. |
| A setting saves but has no effect | Check device capabilities with `openlogi list` or verify device key targeting in `config.toml`. |
| Bindings not updating after editing | Check stderr or run `openlogi reload`. Config reload is transactional: syntax errors are rejected while keeping active bindings intact. |

Restart the login service:

```sh
openlogi service disable
openlogi service enable
```

Service logs currently live at:

```sh
tail -n 80 /tmp/openlogid.stderr.log /tmp/openlogid.stdout.log
```

For direct device diagnostics, stop the daemon before scanning to avoid overlapping hardware requests:

```sh
openlogi service disable
openlogi discover
```

If a foreground daemon is running, stop it with Ctrl+C first. Run `openlogi service enable` after diagnostics if you want login startup restored.

**Recover unresponsive buttons:** disable the service or stop the foreground daemon to request diversion cleanup. If controls remain unresponsive, power-cycle the mouse or reconnect its receiver. Review the configuration before starting OpenLogi again; forced termination cannot guarantee hardware cleanup.

When [reporting an issue](https://github.com/HiteshGorana/openlogi-c/issues), include the OpenLogi/macOS versions, mouse model, connection type, steps to reproduce, and relevant logs. Remove serial numbers, private paths, and script/text payloads before sharing diagnostics.

## Update and uninstall

Rerun the installer to update. It preserves the configuration and attempts to restart an existing installation. It includes rollback on detected installation failures; daemon readiness verification is still planned. Recheck permissions and `openlogi status` after an upgrade.

To choose a custom directory:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh -s -- --prefix "$HOME/tools/openlogi"
```

Use the same `--prefix` on later updates. To pin a version, pass `--version` followed by a published tag from the releases page.

To uninstall the default prebuilt installation, first run `openlogi service disable` and stop any separately launched foreground daemon. Remove these files from `~/.local/bin`:

- `openlogi`
- `openlogid`
- `openlogictl`
- `openlogi_discover`

Use your chosen prefix for a custom installation. Keep `~/.config/openlogi/config.toml` for a future reinstall, or remove it separately if no longer needed.

## Development

### Build from source

Install Xcode Command Line Tools if needed:

```sh
xcode-select --install
```

Build the runtime:

```sh
git clone https://github.com/HiteshGorana/openlogi-c.git
cd openlogi-c
make tools
./bin/openlogi --help
```

The build uses C99 with `-Wall -Wextra -Werror -pedantic`, macOS system frameworks, and the vendored `yyjson` and `tomlc99` libraries. Grant permissions to the source-built executable before testing real input.

For a foreground development session, stop an installed daemon first, then run `./bin/openlogi daemon --verbose`. Use `./bin/openlogi status` from a second terminal. A service enabled with `./bin/openlogi service enable` points at that build directory; rebuilding or moving it affects the service.

| Task | Command |
| --- | --- |
| Build tools and test executables | `make all` |
| Run the C suites (20 suites / 107 tests) | `make test VERBOSE=1` |
| Rebuild/run with ASan and UBSan | `make asan VERBOSE=1` |
| Run offline installer checks | `sh tests/test_install.sh` |
| Run offline release-publishing checks | `sh tests/test_release.sh` |
| Strip the built executable | `make strip` |
| Install a source build | `make install PREFIX="$HOME/.local"` |
| Uninstall from a source checkout | `make uninstall PREFIX="$HOME/.local"` |

**Test isolation and safety:** all 20 test suites (107 tests) use dry-run mock injection and mock HID backends, ensuring tests execute safely and isolated without triggering real macOS spaces, windows, or hardware events. Vendored parsers (`yyjson`, `tomlc99`) are fully instrumented under Clang AddressSanitizer and UndefinedBehaviorSanitizer (0 leaks, 0 errors).

The [macOS CI workflow](.github/workflows/macos.yml) builds and tests with Clang, runs installer checks, and runs ASan/UBSan. The [release workflow](.github/workflows/release.yml) tests Apple Silicon and Intel builds, packages ad-hoc-signed binaries, and publishes archives and checksums for version tags. Physical hardware and permission behavior require separate verification.

Release publishing creates a draft, uploads both architectures and checksums, then publishes it. Retries resume an unfinished draft or skip an already-published release, preserving its downloads. Publishing jobs for the same tag run serially. Use a new version tag for a new release; documentation-only changes can stay on `main` without creating or moving a release tag. Rerunning an older Actions run still uses that run's original workflow revision.

### Repository map

| Path | Purpose |
| --- | --- |
| `src/daemon/` | Startup, runtime dispatch, and shutdown. |
| `src/cli/` | Unified CLI, setup menu, service control, and scanner. |
| `src/hid/`, `src/platform/macos/` | HID++ protocol, mocks, IOKit transport, and app observation. |
| `src/core/` | Configuration, gesture/button state, scrolling helpers, and lifecycle utilities. |
| `src/hook/`, `src/inject/` | macOS input capture and action output. |
| `src/ipc/`, `include/` | Local JSON-RPC client/server and C interfaces. |
| `tests/` | C suites and installer regression checks. |
| `vendor/`, `packaging/`, `scripts/` | Vendored parsers, service definitions, and release packaging. |

### Local IPC

The daemon exposes newline-delimited JSON-RPC over a UNIX socket at `~/.local/state/openlogi/openlogi.sock`, or `$XDG_STATE_HOME/openlogi/openlogi.sock` when set.

| Method | Purpose |
| --- | --- |
| `openlogi.get_status` | Version, uptime, reported permissions, hook state, and foreground application. |
| `openlogi.get_inventory` | Cached device inventory and settings. |
| `openlogi.set_dpi` | Request a sensor DPI change. |
| `openlogi.set_smartshift` | Request a wheel mode/threshold change. |
| `openlogi.reload_config` | Request configuration reload. |
| `openlogi.subscribe` | Subscribe to daemon notifications. |

For example, send this request followed by a newline on a connected socket:

```json
{"jsonrpc":"2.0","method":"openlogi.get_status","params":{},"id":1}
```

### Contributing and performance

Start with [improvements.md](improvements.md) for prioritized issues, integration gaps, and release gates. [AGENTS.md](AGENTS.md) describes the architecture and development contract; some described guarantees are implementation targets still being completed.

Keep changes focused, add regression coverage for behavior changes, and run the relevant checks. Run `make asan` before a commit or pull request as required by the project guide. Changes to device behavior also need a recorded hardware/transport verification.

Small binaries and low overhead are design goals. Earlier footprint comparisons lack a reproducible benchmark record in this tree; measure release builds on a named machine and workload before treating CPU, RAM, or latency figures as guarantees. The roadmap defines proposed performance budgets and how to measure them.
