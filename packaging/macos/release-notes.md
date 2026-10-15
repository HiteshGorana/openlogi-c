### OpenLogi v0.1.11 — Smooth Permission Setup & Non-Blocking Event Tap Hook

This release resolves the system freeze during Accessibility permission setup, eliminates synchronous WindowServer deadlocks in the event tap, and fixes service management commands:

#### Highlights:
1. **Eliminated WindowServer Deadlock in Event Tap Hook**:
   - Replaced synchronous `[NSWorkspace sharedWorkspace] frontmostApplication` calls within high-frequency event tap callbacks with `app_watch_get_cached()`.
   - Prevents recursive Mach/XPC round-trips to WindowServer while an active Quartz event tap filter is processing events, eliminating system-wide input freezes.
2. **Fixed Permission Modal Level & Click-Jacking Suppression**:
   - Removed elevated window levels (`setLevel: 8`) from alert dialogs, allowing unobstructed user interaction with macOS System Settings.
   - Added gentle background polling loops when launching System Settings, ensuring users can toggle Accessibility and Input Monitoring without competing with modal capture loops.
3. **Robust Service Start & Restart**:
   - Added `openlogi start` CLI command that properly manages launchd state.
   - Automatically re-generates and bootstraps `~/Library/LaunchAgents/io.github.openlogi.agent.plist` if absent or previously uninstalled when running `openlogi start` or `openlogi service start`.
   - Automatically clears stale launchd state before bootstrapping to guarantee reliable restarts.
4. **Zero Dependencies & Pure C Footprint**:
   - Standard C99 compliance, under 500 KB stripped binary, ~1.8 MB resident RAM, and 0.00% idle CPU.

#### Installation:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

Or download `OpenLogi-arm64.dmg` / `OpenLogi.app` directly from the release assets below into `/Applications`.

Your TOML configuration in `~/.config/openlogi/config.toml` is preserved across updates.
