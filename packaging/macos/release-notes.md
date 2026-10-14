### OpenLogi v0.1.7 — Zero-Touch App Bundle, Trackpad Pass-Through & Native Permissions

This release brings zero-terminal operation, seamless permission handling, and fluid trackpad pass-through to OpenLogi Pure C:

#### Highlights:
1. **Zero-Touch `OpenLogi.app` Lifecycle**:
   - Double-clicking `OpenLogi.app` automatically prompts for **Accessibility** and **Input Monitoring** permissions, opening the exact System Settings panes.
   - Automatically writes and configures login autostart (`~/Library/LaunchAgents/io.github.openlogi.agent.plist`) with `SuccessfulExit = false` so clean stops stay stopped, while reboots and crashes auto-restart.
   - Opening `OpenLogi.app` while already running queries live mouse status and battery percentage over IPC, offering quick **[Keep Running]**, **[Stop OpenLogi]**, and **[Uninstall]** options.
2. **Apple Silicon & Trackpad Pass-Through**:
   - Accurately preserves trackpad gestures (`kCGScrollWheelEventScrollPhase`, `kCGScrollWheelEventMomentumPhase`, continuous scroll).
   - In-place scroll wheel inversion exclusively targets discrete mouse wheels without interfering with trackpad momentum.
3. **Reliable Input Monitoring Access**:
   - Uses official Apple `IOHIDRequestAccess` and `IOHIDCheckAccess` APIs to ensure raw HID++ Bluetooth and USB communication works reliably.

#### Installation:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

Or download and open `OpenLogi.app` directly into `/Applications`.

Your TOML configuration in `~/.config/openlogi/config.toml` is preserved across updates.
