# Lightweight standby when no device is connected

## Goal

Keep OpenLogi available to detect a supported Logitech device and serve IPC requests, while suspending input processing and application tracking when no usable device is online. Connecting or waking a device should restore its configured behavior automatically.

This is an implementation plan; it does not change daemon behavior yet.

## Current behavior

In `src/daemon/main.c`, startup installs the input hook and starts the foreground application watcher before probing hardware. The main loop services the macOS run loop with a 50 ms timeout, ticks button timing, checks the foreground app about every 100 ms, and probes hardware about every two seconds when `device_count == 0`.

The disconnect callback marks a device offline but does not remove its inventory entry. Therefore, `device_count` is not an online-device count: it can stay nonzero after the last device disconnects. The current fallback rescan condition does not cover that case.

The macOS transport already delivers IOKit connection and removal callbacks. Keeping that transport and its owning run loop alive enables reconnection without keeping the input hook active.

## Runtime states

Use two explicit daemon states:

| State | Entry condition | Services |
| --- | --- | --- |
| `STANDBY` | No supported, usable device is online | HID connection monitoring, IPC, config watcher, process lock, signal handling |
| `ACTIVE` | At least one supported, usable device is online | Standby services plus input hook, app watcher, button/gesture/scroll processing |

Count online, successfully probed devices with usable handles. Do not use inventory length, configuration entries, or the presence of an empty Bolt/Unifying receiver as evidence that input handling is needed. Receiver presence and paired-device availability must be distinguished during probing.

Keep the action worker available initially: it already waits on a condition variable when its queue is empty. There is no need to recreate it on each connection. Measure its idle cost before considering lazy startup.

`ACTIVE` describes device availability, not successful remapping. Report hook/permission failures separately; a connected device with missing permissions must not appear as standby merely because its hook cannot start.

## Implementation steps

### 1. Centralize state reconciliation

Add daemon-owned state and a helper such as `daemon_reconcile_runtime_state()`. Call it after initial probing, successful reprobes, disconnect processing, and wake/reconnect handling. Make transitions idempotent so duplicate notifications do not start extra hooks or watchers.

Keep transitions on the HID/main-loop owner. Hotplug callbacks should record pending changes and wake the owner; avoid stopping threads, performing synchronous probes, or making HID requests recursively inside IOKit callbacks. Audit the existing connect callback, which currently calls `daemon_probe_hardware()` directly.

Copy device identity into pending events; do not retain callback-owned pointers. The macOS removal callback frees the transport object after notifying the daemon, so invalidate references immediately and defer only work that does not dereference the removed handle. Match physical devices/collections by stable transport identity rather than vendor/product alone, which can confuse two identical devices. Reconcile probe results without leaking or overwriting existing live handles, and treat an empty enumeration as an availability update rather than retaining stale online entries.

### 2. Start in standby

Initialize configuration, IPC, the config watcher, signal handling, and HID monitoring first. Probe devices, then reconcile the state. Start the hook and application watcher only if a usable device is online.

Check startup errors explicitly. If connection notifications cannot be established, retain a bounded fallback rescan and expose the degraded monitoring state rather than silently waiting forever.

### 3. Activate safely

On the first usable connection:

1. Probe capabilities and establish live handles.
2. Refresh the current foreground application and resolve the effective profile.
3. Start the input hook and confirm that it is running.
4. Apply hardware settings and enable only the diversions whose consumers are ready.
5. Enter `ACTIVE` and publish status.

Audit `daemon_probe_hardware()` so it does not enable diversion before the relevant input consumer is ready. If permissions or hook startup fail, keep ordinary hardware behavior usable and report the failure. Hardware-only settings may still work; do not claim remapping is operational.

Verify that `hook_init()`/`hook_start()` after `hook_stop()` and repeated app-watcher starts are supported. Fix partial initialization cleanup before relying on these APIs for repeated transitions.

`hook_start()` currently returns after thread creation, before the hook thread sets `g_is_running`. Add a startup acknowledgment and a stop-during-start handshake; an immediate status check can otherwise report false, and a rapid disconnect can attempt to join a thread before its run loop is available to stop. Do not busy-wait for readiness.

### 4. Suspend after the last disconnect

If another usable device remains online, stay active. Otherwise:

1. Stop accepting new device-originated actions and invalidate stale device work.
2. Release synthetic held keys/modifiers and cancel button, gesture, and scroll state for disconnected devices.
3. Apply an explicit policy for accepted actions: cancel pending device actions by device/session identity, while allowing an already running shell command or application launch to finish. If workflows need cancellation, check a session token between steps. Do not block standby waiting for a long command or promise to undo its external effects. Preserve independently accepted non-device work.
4. Undivert any still-accessible hardware before disabling its consumer. Do not write through handles invalidated by physical removal.
5. Stop and join the input hook, stop app tracking, and enter standby.

Cleanup must also work when a device disappears while a button or `HoldShortcut` is held. Reconnection starts with fresh runtime state. Preserve loaded configuration and inventory information as appropriate.

Perform device-specific cleanup on every disconnect, including when another device remains active. The current action queue stores only a cloned action, with no origin/session metadata or cancellation API; those capabilities require implementation if pending actions are to be canceled. `action_queue_wait_idle()` and `action_queue_shutdown()` are not cancellation mechanisms and can wait for long-running work. Quiesce input producers before releasing held input so concurrent callbacks cannot recreate the state being cleared.

### 5. Remove periodic standby work

In standby, skip button ticks, app polling, and active scroll processing. Replace the unconditional 50 ms loop with an event-driven wait:

- On macOS, keep servicing the IOKit-owning CFRunLoop. Add a source to wake it for shutdown and pending state changes; use a timer only for an actual retry deadline.
- On Linux, inspect transport hotplug support and use a readiness/event wait with a shutdown wakeup. If reliable notifications are unavailable, use a slower bounded fallback scan rather than the active 50 ms tick.
- Use elapsed-time deadlines instead of `loop_tick % N`, because loop iteration counts stop representing time once waits vary.
- Allow an explicit refresh, system wake, or a connection event to request a probe. Avoid repeatedly opening HID devices during healthy standby.

IOKit attachment/removal notifications alone do not establish paired-device availability: a mouse can sleep or reconnect while its USB receiver stays attached. Identify and implement receiver-slot connection notifications and wake handling before removing the existing rescan. Where notifications are unavailable or unreliable, keep a documented slow retry deadline while a receiver is present. Do not expand this change into full receiver support without first checking what the current probe supports; it currently probes index `0xFF`, not all paired slots.

The IPC thread also currently uses `poll(..., 100)`, independently of the main loop. Audit its config-watch integration and change that timeout to an indefinite or nearest-deadline wait where safe; retain the existing stop pipe. Check the standalone config-watch path too, which has a one-second timeout. Removing only the daemon tick will not eliminate all periodic wakeups.

Do not replace the loop with a long sleep: that would delay signal handling and reconnection. Audit IPC/config callbacks and shared daemon state before adding wakeups; keep HID operations on their owner and synchronize cross-thread requests.

Use an async-signal-safe shutdown notification, such as a nonblocking self-pipe write from the handler to a run-loop source. A flag alone cannot reliably wake an indefinitely waiting loop, particularly when the signal arrives on another thread. Keep CFRunLoop calls, joins, and hardware teardown outside signal handlers; audit the current second-signal cleanup path, which calls teardown from the handler. Wake sources must be installed before blocking, and pending work must be checked before each wait to prevent lost wakeups.

Keep the current active cadence initially to limit scope. Active-mode timer optimization can be a separate change.

### 6. Make standby visible

Extend `openlogi.get_status` with an additive field such as `runtime_state: "standby"` or `"active"`. Its existing `devices_online` calculation already counts online entries, and `hook_installed` already uses `hook_is_running()`; preserve those semantics. The status handler currently queries the foreground app on demand, which can remain available in standby without restarting periodic tracking. Add new status data through a synchronized snapshot rather than racing daemon transitions.

Log transitions once, for example: `No usable devices online; entering standby` and `Device available; activating input handling`. Preserve IPC compatibility and avoid repeated idle log messages.

## Suggested change locations

| Location | Work |
| --- | --- |
| `src/daemon/main.c` | State reconciliation, startup ordering, device availability, transition cleanup, deadline-based wait |
| Hook and app-watcher implementations | Repeatable start/stop and failure cleanup |
| Platform HID transports | Reliable notifications, receiver/device availability, wake/retry handling |
| Button/gesture/scroll and action-queue code | Disconnect cancellation, held-key release, stale-work prevention |
| IPC status implementation and its context header | Runtime-state snapshot and readiness-based IPC wait |
| `tests/` | Lifecycle and transition regression coverage |

## Validation

Add meaningful mock-backed tests for:

- Startup with no device: standby services remain available; input hook and app watcher stay stopped.
- First connection: one activation, current profile applied, diversion enabled only after its consumer is ready.
- Last disconnection: standby reached even when inventory entries remain; held inputs released and stale work canceled.
- Multiple devices: removing one does not suspend the remaining device.
- Repeated connect/disconnect notifications and cycles: no duplicate resources, leaks, or stale state.
- Disconnect during hook startup and pending-event processing: no blocked joins or use of freed transport objects.
- Hook/permission failure: no unsafe diversion, with successful recovery after a later retry.
- Empty receiver, sleeping paired device, wake/reconnect, and fallback-monitor failure behavior.
- Shutdown from standby: the blocking wait wakes promptly and cleanup completes.
- Long-running actions during disconnect: standby does not wait for them; pending cancellation follows the documented policy.
- Configuration reload and status requests in standby, followed by connection using the latest configuration.

Run `make test` and `make asan`; retain strict C99 warning checks. Before a commit or PR, sanitizer verification is required by `AGENTS.md`.

Measure the current daemon and the changed daemon under the same conditions: no receiver/device, empty receiver, disconnected device after prior activity, and one active device. Record CPU, wakeups, thread count, RSS, and connection-to-ready latency over a representative idle interval. Verify real Bluetooth and receiver reconnects in addition to mock tests.

Acceptance means standby has no active input hook or foreground-app polling, no unconditional 50 ms timer, working IPC/config reload, prompt shutdown, and automatic restoration on connection. Report measured idle costs rather than promising exactly zero CPU or a fixed memory saving. Verify release size with `make strip` if the implementation changes footprint.

## Recommended delivery order

1. Implement and test state transitions while retaining the existing loop cadence.
2. Replace standby polling with notification-driven waiting and test wakeups/retries.
3. Measure idle overhead and reconnect latency on hardware, then document the results.

This order separates lifecycle correctness from wait-loop changes and makes reconnect regressions easier to diagnose.
