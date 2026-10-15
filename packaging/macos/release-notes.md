### OpenLogi v0.1.10 — Native AppKit Alerts & Zero-Hanging UI

This release eliminates LaunchServices timeouts, "application not responding" errors, and AppleEvent hangs by replacing subprocess pipes with native AppKit alert dialogs:

#### Highlights:
1. **Native AppKit In-Process Modal Alerts (`NSAlert`)**:
   - Replaced `osascript` subprocess calls with native AppKit `NSAlert` dialogs executed via C99 Objective-C runtime (`objc_msgSend`).
   - Runs a responsive modal event loop (`-[NSAlert runModal]`), continuously pumping WindowServer events and responding to LaunchServices pings.
   - Eliminates AppleEvent deadlocks and `popen()` pipe hangs.
2. **Fixed "Application Not Responding" on Launch / Reopen**:
   - Calls `[NSApp finishLaunching]` and sets `NSApplicationActivationPolicyAccessory` immediately upon bundle launch, clearing LaunchServices launch watchdogs.
   - Reopening `OpenLogi.app` while active smoothly brings the existing modal dialog to the foreground without error.
3. **Clean Service Teardown & Lock Management**:
   - Fixed `Stop OpenLogi` to exclusively target the background daemon (`gui/<uid>/io.github.openlogi.agent`), preventing self-bootout of the running GUI process.
   - Cleanly releases and unlinks `agent.lock` during uninstallation.
4. **Resumable Onboarding Stability**:
   - Step 1 (Welcome), Step 2 (Input Monitoring), Step 3 (Accessibility), Step 4 (Hardware Probe), Step 5 (Verification), and Step 6 (Autostart) now use crisp native dialogs.
   - Full compatibility with macOS 13, 14, and 15+.
5. **Zero Dependencies & Pure C Footprint**:
   - Preserves standard C99 compliance with zero Swift or Cocoa runtime bloat.
   - Resident RAM remains ~1.8 MB with 0.00% idle CPU and under 500 KB stripped binary.

#### Installation:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

Or download `OpenLogi-arm64.dmg` / `OpenLogi.app` directly from the release assets below into `/Applications`.

Your TOML configuration in `~/.config/openlogi/config.toml` is preserved across updates.
