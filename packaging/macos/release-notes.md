### OpenLogi v0.1.9 — LaunchServices Check-In & Clean Process Termination

This release fixes LaunchServices application check-in and prevents macOS "application not responding" errors when reopening or quitting:

#### Highlights:
1. **LaunchServices / WindowServer Check-In**:
   - Calls `TransformProcessType(kProcessTransformToUIElementApplication)` at launch, registering immediately with WindowServer so macOS never flags `OpenLogi.app` as "not responding".
2. **Reliable App Stop & Uninstall**:
   - Automatically unloads RunningBoard (`application.io.github.openlogi.agent.*`) and launchd services before process termination.
   - Waits for daemon termination before deleting files, eliminating Finder "item can't be moved to Bin because it's open" errors.
3. **Sequential Resumable Onboarding**:
   - All improvements from v0.1.8 (step-by-step permission setup, DMG protection, standby input safety).
   - Double-clicking `OpenLogi.app` guides the user through one permission at a time (Input Monitoring first, then Accessibility), avoiding confusing overlapping System Settings panes.
   - Bounded polling with clear **[Check Again]** and **[Finish Later]** options. State is saved to `~/.local/state/openlogi/onboarding.state` so progress is automatically resumed if interrupted or if macOS requires a Quit & Reopen.
   - Probes live Logitech hardware and reports real device model & battery percentage before completing setup.
   - Includes an interactive 6-second live button click verification test.
   - Prompts for login autostart preference explicitly (**[Start at Login]** or **[No, Start Manually]**).
2. **DMG & Temporary Translocation Protection**:
   - Running directly from a mounted DMG or temporary folder displays an installation alert prompting the user to install to `/Applications`, preventing invalid temporary paths from being registered into LaunchAgents.
3. **Daemon Non-Interactivity & Input Safety**:
   - Background daemon processes (e.g. launched by `launchd`) execute strictly noninteractively with zero unexpected permission dialogs.
   - If OS input monitoring permissions are not granted, the daemon strictly stays in `STANDBY` mode. Buttons are never diverted, guaranteeing native mouse clicks and scrolling remain 100% functional while setup is pending.
4. **Command Dispatch & Diagnostics**:
   - Explicit CLI subcommands (`openlogi daemon`, `openlogi status`, `openlogi doctor`, etc.) are prioritized over app bundle GUI handlers, ensuring automated scripts and launchd services run cleanly.
   - In `openlogi doctor`, click test false positives from application switching (`app_activated`) or hook activation have been eliminated.
   - Direct `IOHIDCheckAccess` reporting in IPC `openlogi.get_status` without misleading fallbacks.

#### Installation:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

Or download `OpenLogi-arm64.dmg` / `OpenLogi.app` directly from the release assets below into `/Applications`.

Your TOML configuration in `~/.config/openlogi/config.toml` is preserved across updates.
