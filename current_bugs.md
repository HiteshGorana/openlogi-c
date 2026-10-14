# OpenLogi Current Bugs & Known Issues

This document tracks active defects and architectural gaps identified in the OpenLogi runtime.

---

## BUG-001: Mouse Scroll Inversion Configuration Is Non-Functional (`invert_scroll` / `openlogi scroll invert`)

- **Status:** Resolved / Verified (Phases 2 & 3)
- **Severity:** High (Core input feature fails silently)
- **Component:** `daemon`, `config`, `hook_macos`, `hires_wheel`, `cli`
- **Discovered On:** 2026-10-14 (OpenLogi Pure C v0.1.3-c / macOS 26)
- **Resolved On:** 2026-10-14 (Phases 2 & 3)
- **Affected Devices:** All supported Logitech mice (e.g., MX Master 2S, MX Master 3S, MX Anywhere)

---

### 1. Summary

Setting `invert_scroll = true` in `~/.config/openlogi/config.toml` or executing `openlogi scroll invert true` previously had no effect on physical mouse scrolling because the configuration field was unparsed, the hook ignored scroll events, and hardware Feature 0x2121 was unwired.

---

### 2. Steps to Reproduce (Resolved)

1. Connect a supported Logitech mouse (e.g., MX Master 2S).
2. Execute the CLI command to enable inverted scrolling:
   ```bash
   openlogi scroll invert true
   ```
   *(Or manually edit `~/.config/openlogi/config.toml` under `[devices.default]` and set `invert_scroll = true`)*.
3. Observe CLI output: `[+] Inverted scrolling set to: ON (invert_scroll = true) (daemon reloaded)`.
4. Inspect settings with `openlogi scroll get`: reports `Invert Scroll: ENABLED (true - wheel reversed)`.
5. Scroll the mouse wheel inside any macOS application: mouse wheel scrolling direction is now reversed as expected, while MacBook trackpad scrolling remains in standard natural direction.

---

### 3. Root Cause Analysis

Investigation revealed four distinct failure layers preventing scroll inversion:

#### Layer 1: macOS Accessibility Permission Failure & Passive Hook State
- macOS requires explicit **Accessibility** privileges (`kAXTrustedCheckOptionPrompt`) for `CGEventTapCreate` to intercept and rewrite system input events.
- On the user's system, `openlogi status` reported:
  ```
  OS Input Hook:       PASSIVE
  Permissions:         Accessibility: DENIED | Input Monitoring: GRANTED
  ```
- Because Accessibility was denied, `hook_start()` in `src/hook/hook_macos.c` failed to create the Mach event tap port (`CGEventTapCreate` returned `NULL`).
- The daemon fell back to `PASSIVE` mode, making event interception impossible until permission was granted.

#### Layer 2: TOML Parser Ignored `invert_scroll`
- In `src/cli/openlogictl.c`, the CLI wrote `invert_scroll = true|false` to `~/.config/openlogi/config.toml`.
- However, `config_device_t` and `openlogi_config_t` in `include/config.h` did not have an `invert_scroll` member.
- The TOML parser in `src/core/config.c` never parsed the `invert_scroll` key. The daemon therefore never held the setting in memory.

#### Layer 3: OS Event Tap Did Not Modify Scroll Wheel Events
- In `src/hook/hook_macos.c`, `event_tap_callback()` trapped `kCGEventScrollWheel` and packaged `HOOK_EVENT_SCROLL`, but always passed the original event through unchanged (`return event;`).
- In `src/daemon/main.c`, `on_hook_event()` only checked for `HOOK_EVENT_BUTTON_DOWN`, completely ignoring `HOOK_EVENT_SCROLL`.
- Trackpad continuous gestures were not differentiated from mouse wheel notches.

#### Layer 4: Hardware-Level Hi-Res Wheel Inversion Was Unwired
- `src/core/hires_wheel.c` implemented HID++ 2.0 Feature `0x2121` (`hires_wheel_build_mode_request` with `HIRES_FLAG_INVERTED = 0x01`), but `daemon_probe_hardware()` in `src/daemon/main.c` never invoked it during enumeration.

---

### 4. Completed Remediation

1. **Configuration Model (Resolved - Phase 2)**:
   - Added `bool invert_scroll` to `config_device_t` in [`include/config.h`](file:///Users/hitesh/Documents/openlogi-c/include/config.h).
   - Implemented `invert_scroll = true|false` parsing in [`src/core/config.c`](file:///Users/hitesh/Documents/openlogi-c/src/core/config.c).
2. **Software Event Inversion & Loop Tagging (Resolved - Phase 2)**:
   - Intercepted `HOOK_EVENT_SCROLL` in [`src/daemon/main.c`](file:///Users/hitesh/Documents/openlogi-c/src/daemon/main.c) to inject inverted delta events.
   - Tagged all synthetic events with `OPENLOGI_EVENT_MARKER` (`0x4F4C4F4749`) so the event tap passes them through without infinite recursion.
3. **Trackpad Natural Scrolling Protection (Resolved - Phase 3)**:
   - In [`src/hook/hook_macos.c`](file:///Users/hitesh/Documents/openlogi-c/src/hook/hook_macos.c), checked `kCGScrollWheelEventIsContinuous`. Continuous touch gestures (built-in MacBook trackpad, Magic Trackpad) pass through untouched, ensuring trackpads maintain native macOS Natural Scrolling while only discrete mouse wheels are inverted.
4. **Hardware Inversion (HID++ 2.0 Feature 0x2121) (Resolved - Phase 3)**:
   - Wired [`hires_wheel_build_mode_request()`](file:///Users/hitesh/Documents/openlogi-c/include/hires_wheel.h) into [`daemon_probe_hardware()`](file:///Users/hitesh/Documents/openlogi-c/src/daemon/main.c) to configure firmware-level hi-res wheel inversion on mice advertising Feature `0x2121`.
5. **Truthful Permission Preflight (Resolved - Phase 2)**:
   - Live query of `CGPreflightListenEventAccess()` in [`src/ipc/ipc_server.c`](file:///Users/hitesh/Documents/openlogi-c/src/ipc/ipc_server.c) correctly informs the user if Accessibility or Input Monitoring permissions are missing.

---

### 5. Related Files & References

- [include/config.h](file:///Users/hitesh/Documents/openlogi-c/include/config.h) — Configuration structure definitions
- [src/core/config.c](file:///Users/hitesh/Documents/openlogi-c/src/core/config.c) — TOML configuration parser
- [src/hook/hook_macos.c](file:///Users/hitesh/Documents/openlogi-c/src/hook/hook_macos.c) — Event tap callback and filter
- [src/daemon/main.c](file:///Users/hitesh/Documents/openlogi-c/src/daemon/main.c) — Hook event handler & hardware probing
- [src/core/hires_wheel.c](file:///Users/hitesh/Documents/openlogi-c/src/core/hires_wheel.c) — Feature 0x2121 payload builder
- [src/cli/openlogictl.c](file:///Users/hitesh/Documents/openlogi-c/src/cli/openlogictl.c) — `openlogi scroll invert` CLI command
- [improvements.md](file:///Users/hitesh/Documents/openlogi-c/improvements.md#L39) — Section 2 gap list reference (Item 39)
