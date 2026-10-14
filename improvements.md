# OpenLogi improvements and production readiness plan

Reviewed: 2026-10-14, against commit `7a779c8`.

The immediate goal should be a mouse driver that people can leave running all day: ordinary input continues working, settings actually take effect, reconnects recover automatically, and failures explain how to recover. Build broader keyboard support and advanced customization on that foundation.

This plan comes from source inspection of the daemon, HID transport, input hook, action injection, configuration, IPC, CLI, installer, CI, and tests. **The findings below are static code findings; hardware failures were not reproduced, performance was not measured, and the test suite was not run for this documentation change.** Proposed commands, targets, interfaces, and acceptance thresholds are future work unless explicitly described as existing.

## 1. Priorities and scope

| Priority | Meaning | Completion rule |
| --- | --- | --- |
| **P0 — reliable daily use** | Fix crashes, lost input, incorrect settings, hangs, unsafe lifetime management, and misleading results. | Complete before calling the driver stable. |
| **P1 — usable release** | Make installation, configuration, recovery, diagnostics, and upgrades understandable. | Complete before recommending it broadly to nontechnical users. |
| **P2 — broader capabilities** | Expand keyboard support, device coverage, and advanced customization. | Ship incrementally behind explicit capability checks and validation. |

Preserve the small C99 core, strict warnings, bounded memory, platform boundaries, local operation, and dependency-light runtime. Developer tools and an optional UI can have separate dependencies and size budgets.

Start with a named set of verified Logitech mice on macOS. Publish support by **model, firmware, connection type, macOS version, and feature**. A device being discovered does not prove that remapping, gestures, battery, or scrolling work. Linux packaging exists, but the current tree has no Linux hardware/input backend; keyboard remapping is also a separate implementation project.

## 2. Concrete gaps in the current tree

These findings explain why passing component tests and a small binary do not yet establish a dependable product.

| Finding | Source evidence | User impact / next action |
| --- | --- | --- |
| Reload overwrites the active configuration before parsing succeeds. | `config_parse_string()` in [config.c](src/core/config.c) clears `out_config`; both daemon reload paths pass the shared live configuration. | An invalid edit can erase working bindings. Implement transactional reload and safe publication. |
| Workflow ownership has no production cleanup API. | [config.c](src/core/config.c) allocates workflows; [config.h](include/config.h) has no `config_free()`, despite the architecture guide describing one. | Repeated reloads can lose allocated workflow pointers. Add explicit ownership, destruction, and reload leak checks. |
| Input callbacks can execute slow work directly. | `on_hook_event()` and HID gesture dispatch in [main.c](src/daemon/main.c) call action execution; [inject_macos.c](src/inject/inject_macos.c) uses `system()` and workflow `usleep()`. | Commands and delays can stall input processing. Introduce bounded asynchronous execution. |
| Shared runtime state and HID transaction state lack a single owner. | Hook, config watcher, IPC, and HID callbacks access the daemon context; [hid_iokit.c](src/platform/macos/hid_iokit.c) has one mutable pending transaction per handle. | Concurrent reloads, input, and settings writes can race or overwrite a transaction. Serialize device work and define snapshot lifetimes. |
| Removal frees handles still referenced elsewhere. | `on_device_removal()` in [hid_iokit.c](src/platform/macos/hid_iokit.c) frees a handle; the daemon retains `g_live_handles`, and the diversion registry retains handle pointers. The daemon does not register the transport hotplug callback. | Later settings changes or cleanup can dereference stale handles. Make removal cancel work and invalidate all owners before freeing. |
| Discovery and reconnection are incomplete at daemon level. | [main.c](src/daemon/main.c) rescans only while `device_count == 0` and probes with device index `0xFF`. | A second device, a returning device, or a paired receiver slot may not become usable. Integrate hotplug, wake, and receiver-slot discovery. |
| Gesture controls are diverted unconditionally during probing. | `daemon_probe_hardware()` in [main.c](src/daemon/main.c) diverts gesture and mode-shift controls without resolving a capture plan or checking usable bindings. | Native controls can be taken away without a working replacement. Make diversion conditional and reversible. |
| Tested runtime components are not wired into the daemon. | [main.c](src/daemon/main.c) does not use the button runtime, capture planner, reapply planner, armed-session persistence, or scroll pipeline. | Component test coverage does not prove that those advertised behaviors work in daily use. Add full-path tests and integrate deliberately. |
| Button lifecycle and action semantics are incomplete. | `on_hook_event()` handles mappings on down only; [inject_macos.c](src/inject/inject_macos.c) ignores the parsed `hold` flag. Its `ShowActionsRing` case is a no-op, with no daemon implementation. | Held shortcuts behave like taps; some advertised actions do nothing. Define and test actual effects. |
| Input source and device identity are lost. | [main.c](src/daemon/main.c) resolves bindings with a null device key, uses one global gesture accumulator, and applies some hardware actions to every device. [hook.h](include/hook.h) lacks device identity. | Multiple mice can receive the wrong profile or setting; unrelated devices can be affected by global interception. |
| Synthetic mouse events have no loop-prevention marker. | [inject_macos.c](src/inject/inject_macos.c) posts events; [hook_macos.c](src/hook/hook_macos.c) does not recognize OpenLogi-generated events. | A mapping that injects an intercepted button can trigger itself repeatedly. Tag and bypass internal events. |
| Hardware-setting success is unreliable. | `daemon_set_dpi()` and `daemon_set_smartshift()` update cached state before checking hardware results, discard write results, and return true even without a matching device. | The CLI can claim success without changing the device. Return verified outcomes and preserve last confirmed state. |
| Scroll settings are exposed without a complete runtime path. | [openlogictl.c](src/cli/openlogictl.c) writes scroll preferences; [config.h](include/config.h) has no corresponding scroll configuration fields, and the daemon does not process scroll events. | Users can save settings that have no effect. Connect configuration, hardware mode, processing, and injection. |
| IPC has a concrete request-buffer bounds defect. | `ipc_client_call_raw()` in [ipc_client.c](src/ipc/ipc_client.c) writes `snprintf()`'s returned length from a fixed 256-byte array without checking truncation. | Long requests can cause an out-of-bounds read. Fix before extending RPC functionality. |
| IPC does not fully handle stream behavior. | [ipc_server.c](src/ipc/ipc_server.c) ignores partial writes and subscribes by substring; [ipc_client.c](src/ipc/ipc_client.c) only polls before its blocking byte-by-byte read loop. | Slow or partial messages can hang clients, lose responses, or produce incorrect subscriptions. Add framing, deadlines, and output queues. |
| Permission reporting is misleading. | `handle_get_status()` in [ipc_server.c](src/ipc/ipc_server.c) initializes Input Monitoring to true without checking it. | Setup can look healthy while input capture fails. Report measured permission and subsystem states. |
| Test results overstate what was verified. | [run_tests.sh](tests/run_tests.sh) prints hardcoded counts and sanitizer success; the vendor compile rule in [Makefile](Makefile) omits sanitizer flags. Some tests invoke real OS injection. | Green output is not evidence of instrumented vendor code, leak freedom, or safe daemon behavior. Fix test isolation and reporting. |
| Release trust and readiness checks are incomplete. | [package_release.sh](scripts/package_release.sh) uses ad-hoc signing. [install.sh](install.sh) has checksum verification and rollback, but a background daemon's parent can return success before child initialization finishes. | Updates can disturb permission grants or appear successful when the new daemon never becomes ready. Add signed distribution and health-checked updates. |

## 3. P0: make input and runtime behavior dependable

### P0-01. Give mutable runtime state a clear owner

- [ ] Put device handles, connection generations, HID transactions, and hardware settings under one runtime owner. Marshal IPC and input requests to it.
- [ ] Give input callbacks a safely published, immutable binding snapshot. Define how queued actions retain their configuration generation until completion.
- [ ] Replace cross-thread `volatile bool` flags with proper synchronization. Stay compatible with C99; do not introduce unguarded C11 atomics or ad hoc lock-free code.
- [ ] Document ownership and shutdown order for configuration, handles, callbacks, subscriptions, and child processes. Never hold a global client lock while invoking device work.
- [ ] Keep per-device transactions serialized, reject stale-generation responses, and keep unsolicited reports separate from command replies.

**Acceptance:** concurrent input, reload, disconnect, and settings writes complete without races, stale accesses, deadlocks, or cross-device responses under stress tests and a separate ThreadSanitizer job.

### P0-02. Make configuration changes transactional

- [ ] Parse into a separate heap-owned configuration, validate all fields, prepare the runtime changes, and only then publish it.
- [ ] On failure, retain the last working configuration and capture plan. Return a useful error identifying the file and offending field; include line/column information where available.
- [ ] Implement `config_free()` plus explicit clone/move rules for every workflow and nested action. Release old snapshots only after their readers and queued actions finish.
- [ ] Use one reload path for the watcher, CLI, and IPC. Track active generation, last successful reload, and last rejection.
- [ ] Watch the parent directory as well as file replacement. Recover when the file is initially missing, deleted, replaced after a delay, or saved repeatedly.
- [ ] Validate schema versions, unknown actions/buttons, duplicate bindings, preset indices, string lengths, limits, and numerical ranges. Do not silently truncate a workflow or discard a misspelled setting.

**Acceptance:** invalid edits leave working input untouched; creating a missing file activates it; 10,000 valid/invalid reloads with workflows show no leaks, lost bindings, or growing handle counts.

### P0-03. Take over a control only when its replacement is ready

- [ ] Integrate and extend `capture_plan.c` so only configured, supported controls are diverted.
- [ ] Confirm required permissions, input processing, and action support before arming. Preserve ordinary hardware behavior when prerequisites are absent.
- [ ] Capture previous reporting state where supported, apply only required changes, and verify results. Roll back partial arming failures.
- [ ] Reserve diversion-registry capacity before changing firmware; reject additional diversion safely if tracking would overflow.
- [ ] Define native fallback for a diverted button with no binding in the active application. A union of all per-app controls alone does not solve fallback.
- [ ] Preserve core pointing and clicking by default. Advanced primary-button remapping should require an explicit user choice and a tested escape route.

**Acceptance:** an empty configuration, unsupported action, missing permission, or failed arm operation leaves the mouse usable and reports the precise reason.

### P0-04. Handle the complete device lifecycle

- [ ] Implement explicit states such as discovered, probing, ready, armed, sleeping, disconnected, and degraded.
- [ ] Connect transport hotplug callbacks to inventory and runtime state. On removal, cancel transactions and actions, release held input, detach diversion entries, and invalidate references before freeing handles.
- [ ] Integrate system sleep/wake and device reconnect handling. Rediscover feature indices and capabilities before reapplying settings; do not reuse stale firmware state.
- [ ] Discover receiver slots as well as direct devices; distinguish physical devices from their multiple HID collections.
- [ ] Use persistent identity where available, with explicit receiver-slot identity and documented fallback when serials are absent. Do not equate identical VID/PID pairs with the same physical mouse.
- [ ] Retry temporarily unavailable devices with bounded backoff. One unresponsive device must not stop input or configuration for others.
- [ ] Refresh battery/charging and online state using supported notifications or conservative polling, with timestamps and unknown/stale states.

**Acceptance:** repeated unplug/replug, power cycling, receiver removal, Bluetooth reconnect, and sleep/wake recover without restarting the daemon. Two identical devices keep separate profiles.

### P0-05. Complete button, gesture, and held-key lifecycles

- [ ] Integrate `button_runtime.c` and keep state per device and control rather than using one global gesture accumulator.
- [ ] Process down, up, drag, long press, repeat, cancellation, and lost release events. Capture the chosen action/profile at press time so an app switch cannot release the wrong action.
- [ ] Implement true held shortcuts, including modifier-only holds. Track exactly which keys/modifiers OpenLogi owns and release them on disconnect, pause, reload, cancellation, and exit.
- [ ] Suppress a complete physical event sequence when remapping; avoid leaking an unmatched original release or emitting an extra click after a gesture.
- [ ] Stamp OpenLogi-generated events with an internal marker and bypass reprocessing. Coordinate HID and OS sources to avoid duplicate action delivery.
- [ ] Preserve physical modifiers already held by the user. Do not use a broad “release every key” routine as normal cleanup.
- [ ] Make gesture thresholds and hold timing configurable with sensible bounds and a calibration view.

**Acceptance:** each gesture or click produces its specified effect exactly once; hold/release remains balanced through app switches and failures; mappings to the same mouse button never recurse.

### P0-06. Keep slow work away from input callbacks

- [ ] Limit callbacks to bounded event decoding, snapshot lookup, suppression decisions, and queueing. Cache the foreground application instead of querying NSWorkspace on every press.
- [ ] Run workflows with a timer-driven scheduler and bounded action queues. Preserve step ordering without sleeping an input or HID thread.
- [ ] Use `posix_spawn()` or an equivalent controlled process launcher with explicit arguments for applications and AppleScript. Keep intentional shell execution confined to `RunShellCommand`.
- [ ] Add command deadlines, cancellation, child reaping, bounded output capture, concurrency limits, and meaningful completion errors.
- [ ] Define queue overflow behavior: preserve releases and cancellation, coalesce only events whose semantics allow it, and make overload visible. Do not silently drop a held-key release.
- [ ] Prevent child processes from inheriting IPC sockets, device-related descriptors, or shutdown pipes; retain the existing lock-descriptor close-on-exec behavior.

**Acceptance:** a 30-second command, a stalled script, or a large workflow does not freeze pointer input, block unrelated actions, or prevent shutdown.

### P0-07. Report what the hardware actually did

- [ ] Return structured failures for missing device, unsupported capability, invalid value, device asleep, transport timeout, and firmware rejection.
- [ ] Validate original numeric values before narrowing casts. Validate DPI presets against the discovered sensor range and supported steps.
- [ ] Keep requested and confirmed settings separate; change confirmed state only after an acknowledged successful transaction and readback where supported.
- [ ] Apply a hardware action to its originating device. Require an explicit all-devices operation for broadcasting a setting.
- [ ] Distinguish temporary changes from persisted preferences. Reapply persisted settings after reconnect using the same validated path.
- [ ] Resolve `SetDpiPreset`'s contract: the parser currently stores a preset index, while the guide describes an exact DPI value. Pick one documented meaning and provide an explicit migration if it changes.

**Acceptance:** unknown device IDs and failed writes produce nonzero CLI exits and structured RPC errors, without changing displayed confirmed state.

### P0-08. Make every exposed setting and action functional

- [ ] Trace every public option through CLI/UI, config parsing, profile resolution, runtime execution, and verification. Hide or label incomplete functionality until that path exists.
- [ ] Integrate scroll configuration, hi-res mode negotiation, wheel decoding, scaling, fractional accumulation, momentum scheduling, and proper phase termination.
- [ ] Separate mouse wheel, thumbwheel, and trackpad behavior. Where the OS event source cannot reliably identify a device, expose that limitation instead of silently applying a device profile globally.
- [ ] Test immediate direction changes, app switches during momentum, ratchet/free-spin transitions, horizontal scrolling, and stop-on-new-input behavior.
- [ ] Convert UTF-8 to valid UTF-16 for text injection, including surrogate pairs. The current byte-to-`UniChar` cast is insufficient for non-ASCII text.
- [ ] Audit click counts, application bundle IDs versus paths, customized system shortcuts, and action availability across supported OS versions.
- [ ] Implement `ShowActionsRing` through an optional UI, or mark it unavailable when no compatible UI is connected.

**Acceptance:** every feature listed as supported has a full-path regression test; scroll settings visibly change scrolling and Unicode text is emitted correctly.

### P0-09. Repair IPC bounds, framing, and backpressure

- [ ] Replace the fixed request buffer defect with bounded JSON serialization and explicit oversize errors; audit other `snprintf()` return values and manual JSON construction.
- [ ] Implement partial reads/writes, `EINTR`/`EAGAIN` handling, per-client output queues, message-size limits, and end-to-end monotonic deadlines.
- [ ] Disconnect or throttle slow subscribers without blocking input or unrelated clients. Bound queued bytes as well as client count.
- [ ] Parse subscription methods structurally. Validate request version, IDs, method parameters, numeric ranges, and response/request ID matching.
- [ ] Handle notifications without replies and define batch behavior according to the [JSON-RPC 2.0 specification](https://www.jsonrpc.org/specification).
- [ ] Use restrictive per-user runtime directories, verify ownership, reject overlong socket paths, and check peer credentials where supported. Replace the shared `/tmp/openlogi.sock` fallback with a private per-user location.
- [ ] Check existing socket/lock ownership before cleanup. Protect against unsafe symlink handling and concurrent-start races.

**Acceptance:** long parameters, malformed JSON, one-byte fragments, half-closed clients, full buffers, and unread subscriptions cannot crash or indefinitely stall the daemon or CLI.

### P0-10. Make shutdown and crash recovery bounded

- [ ] Have signal handlers only set flags or notify a self-pipe. The current second-signal path calls cleanup that can take locks and perform hardware I/O inside the handler.
- [ ] Route normal exit and every startup failure through one idempotent cleanup path, including IPC startup failure after hardware diversion.
- [ ] Stop new work, cancel workflows, release owned input, undo diversion while handles remain valid, unregister callbacks, close handles, and then release the process lock.
- [ ] Bound teardown time across all controls and disconnected devices. Record unconfirmed restoration rather than silently declaring success.
- [ ] Integrate crash/session persistence with ownership and device generations. On restart, inspect and repair OpenLogi's previous state without blindly resetting another driver's configuration.
- [ ] Add a user-accessible recovery path and document hardware power cycling as a last resort. `SIGKILL`, process crashes, and power loss cannot be handled by an in-process signal handler; test restart recovery separately.
- [ ] Add startup readiness acknowledgement and crash-loop backoff. After repeated failures, start in a visible passive recovery mode.

**Acceptance:** every startup stage can fail without leaving live callbacks or untracked diversion; ordinary shutdown is bounded; crash recovery has a tested procedure and an honest outcome.

### P0-11. Measure permissions and operational health

- [ ] Check Accessibility and Input Monitoring independently using supported platform APIs, including [CGPreflightListenEventAccess](https://developer.apple.com/documentation/coregraphics/cgpreflightlisteneventaccess%28%29) for the latter where applicable.
- [ ] Distinguish permission denied, hook creation failed, tap disabled, device unavailable, and application/profile inactive.
- [ ] React to grants and revocations without repeated prompt loops. Retry hook setup safely and disarm controls whose replacement path is no longer usable.
- [ ] Count tap timeouts/restarts, hardware timeouts, stale inventory, queue overruns, and rejected reloads.
- [ ] Define health as the required subsystems working, rather than merely an existing PID or a loaded launchd job.

**Acceptance:** status never reports an unchecked permission as granted, and a user can identify which component prevents a binding from working.

## 4. P1: make the product easy to install, understand, and recover

| ID | Improvement | Concrete deliverable / acceptance |
| --- | --- | --- |
| **P1-01** | Guided first-run setup | Detect devices; identify the exact executable needing permissions; guide permission grants; let the user test a button safely; show completion only after input and device control are verified. A clean install works without editing TOML. |
| **P1-02** | Optional menu bar UI | Device/battery status, active profile, permission errors, pause/resume, restore defaults, and a button-map editor. The C daemon remains independently usable; closing the UI does not stop configured input. Support keyboard navigation, VoiceOver labels, contrast, and display scaling. |
| **P1-03** | Live mapping preview | Press a button to highlight it, show which device/profile/action won, and preview the proposed mapping before saving. Include undo and an automatic revert for unconfirmed risky input changes. |
| **P1-04** | Useful CLI diagnostics | Add proposed `openlogi doctor`, `openlogi logs`, and `openlogi config validate` commands. Report executable/version, service ownership, permissions, device capabilities, active configuration, and corrective steps. Keep diagnostics read-only by default. |
| **P1-05** | A direct recovery control | Add proposed `openlogi pause`, `resume`, and `recover` operations, plus an accessible UI control. Pause releases owned input and restores diverted controls; it is not just a flag that discards callbacks. |
| **P1-06** | Clear, consistent configuration | One schema and action registry drive parsing, CLI help, docs, and UI. Add proposed `config explain`, export/import, backups, and explicit migrations. Show whether a value is requested, active, persisted, unsupported, or waiting for a device. |
| **P1-07** | Safe configuration editing | Replace line-oriented settings rewrites with validated structured updates. Preserve unrelated devices/profiles and comments where feasible; serialize concurrent writers; use temporary files, checked writes, atomic replacement, and rollback. |
| **P1-08** | Signed macOS distribution | Add Developer ID signing, hardened-runtime validation, and notarization to CI with a stable identity. Offer an appropriate signed package/app distribution and a maintained package-manager route. Follow [Apple's distribution guidance](https://developer.apple.com/developer-id/). Verify permission behavior across upgrades on real machines; signing is not a guarantee that permission prompts never recur. |
| **P1-09** | Health-checked updates | Preserve the existing checksum verification and rollback. Add daemon readiness and version checks, rollback of config migrations and aliases, concurrent-installer exclusion, and interruption/disk-full tests. Keep a documented previous-version recovery route and stable/beta channels. |
| **P1-10** | Predictable service management | Make enable/disable/restart idempotent. Distinguish installed, loaded, running, ready, and degraded states. Use per-user logs with size/rotation limits instead of unbounded shared `/tmp` logs; uninstall should restore input and offer config retention. |
| **P1-11** | Device and driver coexistence | Detect likely conflicts with other remappers or Logitech software and explain overlapping ownership. Offer per-device opt-out. Never stop another application or rewrite its configuration automatically. Validate coexistence instead of relying on a distinct HID++ software ID alone. |
| **P1-12** | Documentation people can follow | Publish a verified support matrix, a short getting-started guide, task-oriented examples, troubleshooting by symptom, recovery/uninstall instructions, changelog, and known limitations. Make the action vocabulary and schema examples match the parser. |

Suggested first-run flow: **install → detect device → grant required permissions → test input → choose preset → save → verify login startup**. Keep detailed protocol information in diagnostics rather than placing it in the normal setup flow.

## 5. P2: powerful mouse and keyboard support

Implement these in increments after the input-safety and lifecycle work. Each increment needs a real supported-device entry and end-to-end acceptance tests.

- [ ] **P2-01 — Keyboard input architecture.** Add keyboard events and per-device identity to the input abstraction. Preserve key-down/up, modifiers, repeat, media keys, and simultaneous inputs. A global event tap does not by itself establish per-keyboard attribution; validate the platform strategy before advertising isolation.
- [ ] **P2-02 — Practical keyboard remapping.** Support key-to-key mappings, modifier swaps, Fn/media behavior where hardware allows it, and per-app shortcuts. Test ANSI/ISO/JIS layouts, non-US layouts, dead keys, IMEs, and Unicode. Clearly distinguish physical-key mapping from text insertion.
- [ ] **P2-03 — Mouse/keyboard combinations.** Add optional layers, tap-versus-hold, chords, and mouse-button-plus-key actions. Define conflict precedence and bounded recognition delays. Provide an escape mapping that remains available when a layer is active.
- [ ] **P2-04 — Profiles and presets.** Offer reviewed presets for common applications, per-device/app overrides, profile duplication, explicit precedence, and portable export/import. Keep unsupported settings visible with a reason rather than pretending they applied.
- [ ] **P2-05 — Advanced mouse controls.** Add wheel tilt, thumbwheel actions, device-specific extra buttons, precision/DPI hold, configurable gesture sets, and an optional action ring according to discovered capabilities. Preserve pointer movement and native defaults for unconfigured controls.
- [ ] **P2-06 — Workflows users can manage.** Provide a visual sequence editor, execution status, cancellation, repeat limits, and error handling. Preview shell/AppleScript content when importing profiles and require explicit user enablement for executable actions from those imports.
- [ ] **P2-07 — Device capability registry.** Store protocol capabilities and narrowly scoped firmware/model quirks with regression traces. Avoid scattering product-ID conditionals through dispatch. Treat battery, backlight, special keys, and hardware modes as separate optional capabilities.
- [ ] **P2-08 — Safe keyboard boundaries.** Respect secure input and OS-protected contexts; explain when remapping is unavailable. Do not promise login-screen/FileVault support from a user agent or attempt to bypass protected input. Keep typed text and raw key history out of routine logs and support bundles.
- [ ] **P2-09 — Deliberate platform expansion.** First make the platform-neutral core build/test on Linux. Then implement and validate hidraw, evdev/uinput, permissions, configuration watching, service lifecycle, and desktop-specific limitations before enabling a supported Linux release. Treat Windows as a separate future backend project.
- [ ] **P2-10 — Local integrations.** Version the IPC contract and provide small examples for scripts, launchers, and third-party UIs. Use the same validated actions and permissions as the bundled CLI. Keep network/cloud synchronization optional and defer it until the local product is dependable.

## 6. Developer experience and maintainability

### DX-01. Make tests safe to run on a developer's own machine — P0

- [ ] Provide fake injection, fake device transport, fake app/permission state, an injectable monotonic clock, and temporary config/runtime directories.
- [ ] Default tests must not post Mission Control/media/keyboard events, take over connected hardware, change login services, or edit the developer's real configuration.
- [ ] Separate explicit live-input and physical-hardware suites from unit/integration suites. A developer should be able to run the standard checks without granting input permissions.
- [ ] Link the actual daemon runtime into integration tests so a fake button report reaches the real resolver and a recorded output event. Do not stop coverage at independent helpers.

**Done when:** a clean checkout can run its default checks safely and reproduce disconnect/reload/hold failures without physical hardware.

### DX-02. Make builds and checks reproducible — P0/P1

- [ ] Generate header dependencies (`-MMD -MP`) so header changes rebuild consumers correctly.
- [ ] Separate normal, ASan/UBSan, TSan, and release output directories; make the runner and CLI tests honor their configured binary directory.
- [ ] Pass sanitizer instrumentation into vendored code while keeping its warning policy separate. The current vendor rule drops the sanitizer flags.
- [ ] Make test output count actual registered/executed tests, preserve failure logs, and advertise sanitizers only when that invocation actually used them.
- [ ] Fix nonverbose runner error handling so a failing command substitution under `set -e` does not bypass the intended summary.
- [ ] Add one documented proposed `make check` entry point covering strict build, isolated tests, installer tests, and static checks; keep required sanitizer validation explicit and reproducible.
- [ ] Provide a supported toolchain/SDK baseline, a compile database for clangd, formatting configuration, and a short LLDB debugging recipe. Avoid requiring a second build system merely for editor support.

### DX-03. Define boundaries that make fixes smaller — P1

- [ ] Extract lifecycle, input routing, action scheduling, and settings application from `main.c` into testable modules. Keep `main.c` responsible for startup and orchestration.
- [ ] Separate CLI presentation from config updates and service operations. Reuse central action/button metadata instead of maintaining a second hand-written catalog.
- [ ] Give public functions explicit success/error and ownership contracts. Prefer actionable error types over a single boolean for operations that can fail in several ways.
- [ ] Keep host-specific APIs behind adapters. Add C99-compatible compile-time size checks for memory-budget invariants and keep large configuration snapshots off the stack.
- [ ] Check allocation failures and cleanup across C, CoreFoundation, and Objective-C interoperability, including autorelease-pool lifetimes on worker threads.

### DX-04. Test the failure paths that matter — P0/P1

| Test layer | Add coverage for |
| --- | --- |
| Pure unit tests | Action parsing, range checks, profile precedence, button/gesture state transitions, UTF conversion, scroll math, ownership/destruction. |
| Daemon integration | Event → source device → active profile → output; reload while held; settings acknowledgement; config-to-capture-plan changes; pause/recovery; permission transitions. |
| Fault injection | Disconnect during a request, delayed/duplicate/out-of-order responses, stale generations, allocation failure, full queues, malformed reports, full disk, incomplete saves. |
| Process tests | Startup readiness, duplicate instances, startup-stage failures, termination during work, crash restart, service pause, child timeout, partial IPC messages. |
| Installer tests | Existing rollback/checksum coverage plus failed readiness, interrupted replacement, concurrent updates, alias restoration, permission identity, and config migration rollback. |
| Hardware tests | Named mouse/keyboard models, BLE/receiver/wired modes where supported, two identical devices, mixed devices, repeated wake/reconnect, and coexistence. |

### DX-05. Expand automated verification with honest reporting — P1

- [ ] Run PR checks on supported Apple Silicon and Intel configurations and define which OS versions are release-tested. A single moving `macos-latest` runner is insufficient evidence for a broad support claim.
- [ ] Keep ASan/UBSan, add a separate TSan run, and explicitly verify leak-detector availability for the chosen compiler/runtime. If leak detection is unavailable, report that and use an appropriate supported alternative. See [Clang ASan](https://clang.llvm.org/docs/AddressSanitizer.html), [LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html), and [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html).
- [ ] Add fuzz targets for packet unpacking, TOML/action parsing, JSON-RPC dispatch, and state-machine event sequences, with regression corpora and time/memory bounds.
- [ ] Add static analysis and focused coverage for critical failure paths. Avoid treating a global percentage as proof of input safety.
- [ ] Run short deterministic stress checks on PRs and longer soak/hardware checks for release candidates; publish logs and exact binary versions.
- [ ] Track vendored library versions/licenses and security fixes. Pin release automation dependencies and produce artifact provenance plus checksums/signature verification.

### DX-06. Make contributions and support efficient — P1

- [ ] Add `CONTRIBUTING.md` with build/check/debug instructions, architecture ownership rules, and how to add an action, device quirk, or regression trace.
- [ ] Create bug-report templates that collect version, OS, device/transport, expected behavior, actual behavior, and a small reproducer.
- [ ] Generate a local, redacted diagnostics bundle: version/build ID, health, capabilities, recent error counters, and sanitized configuration. Let the user review it before sharing; omit serials, text payloads, executable scripts, and detailed app history by default.
- [ ] Assign an owner and acceptance test to each roadmap item. Keep changes small enough to review and link bug fixes to regressions.
- [ ] Document the release process, compatibility/migration policy, rollback procedure, security-reporting route, and policy for experimental features.
- [ ] Update `AGENTS.md`, README, help text, and test counts from implementation evidence; clearly separate current behavior from intended architecture.

## 7. Performance: measure responsiveness before optimizing size

These are **proposed initial budgets**, not current measurements or promises. Establish a reproducible baseline, then adjust with recorded justification for supported hardware. Measure optimized release builds separately from instrumented builds and from an optional GUI.

| Area | Initial goal | How to measure |
| --- | --- | --- |
| Input callback duration | p99 below 1 ms; no blocking I/O, child waits, or workflow delays | Timestamp callback entry/exit under normal load, continuous scroll, app switching, and IPC pressure. |
| Simple remap overhead | p95 below 2 ms and p99 below 5 ms inside OpenLogi | Measure capture-to-output-enqueue time, excluding intentional hold/gesture delay; report device/OS end-to-end latency separately. |
| Idle CPU | Average below 0.1% of one core over 10 minutes on a named machine | Include connected, disconnected, permission-denied, and subscribed-client cases; publish wakeups alongside CPU. |
| Memory | Aim for daemon RSS below 10 MB in a representative mouse+keyboard workload; no sustained growth | Record RSS and physical footprint separately; compare steady state after repeated reload/reconnect/workflow cycles. |
| Reconnect | p95 below 2 seconds after the OS exposes a responsive supported device | Record OS discovery, probe completion, settings confirmation, and rearm separately. |
| Release binary | Preserve the repository's under-400-KB target for the stripped core | Measure each architecture consistently and record signing/package overhead separately; review justified exceptions explicitly. |
| Shutdown | Aim for completion within 2 seconds during ordinary termination | Inject unavailable devices and slow children; document bounded fallback when restoration cannot be confirmed. |

- [ ] **PERF-01:** replace the 100-ms foreground-app polling loop with notifications where practical; use deadline/event-driven waits instead of periodic wakeups without work.
- [ ] **PERF-02:** avoid per-event allocations, JSON serialization, synchronous logging, and firmware queries in input callbacks; use bounded reusable buffers and asynchronous diagnostics.
- [ ] **PERF-03:** use monotonic time for gestures, transaction deadlines, retries, and workflow scheduling; inject the clock in tests. Wall-clock adjustments must not change hold or timeout behavior.
- [ ] **PERF-04:** benchmark default mappings, full profiles, multiple devices, maximum-rate reports, heavy scrolling, and slow subscribers. Report p50/p95/p99/max, drop counts, queue depth, and conditions instead of only averages.
- [ ] **PERF-05:** retain timestamped benchmark results per release and flag meaningful regressions. Keep README performance comparisons reproducible with equivalent workloads and toolchains.

## 8. Release acceptance gates

Passing these gates establishes evidence for the declared support matrix. It does not establish compatibility with every mouse, keyboard, firmware, or OS.

| Gate | Required evidence before a stable release |
| --- | --- |
| Default input safety | Empty/bad config and missing permissions preserve usable native input; every takeover has a working fallback and recovery route. |
| Full behavior coverage | Each advertised feature has a full-path test and at least one relevant physical-device verification, or is explicitly marked unavailable/experimental. |
| Memory and concurrency | Strict builds and configured sanitizer jobs pass; leak checks report their actual coverage; no known stale handle or data race remains in exercised runtime paths. |
| Reload stability | 10,000 mixed valid/invalid reloads with workflows, active input, and replaced files; no lost working config or resource growth. |
| Input lifecycle | At least 100,000 synthetic mixed press/release/gesture transitions, including cancellations and app switches; no stuck owned keys, recursive mapping, or unintended duplicate output. |
| Device lifecycle | Automated lifecycle stress plus at least 100 physical disconnect/reconnect cycles and 50 sleep/wake cycles per representative supported transport class; zero unexplained stuck-input cases. |
| Multi-device behavior | Two identical mice, mouse+keyboard, and receiver/direct combinations retain independent profiles and do not modify unrelated trackpad input. |
| Failure recovery | Startup failure at each stage, permission revocation, command timeout, normal termination, forced termination, and crash restart have verified bounded outcomes. |
| IPC resilience | Malformed, oversized, fragmented, disconnected, and slow-client scenarios remain bounded; client deadlines and response IDs are correct. |
| Endurance | A 72-hour representative hardware soak with app changes, idle periods, scrolling, reloads, and wake cycles has no crashes, hangs, stuck input, or sustained resource growth. |
| Installation | Fresh install, update from the previous supported release, failed-update rollback, login startup, and uninstall succeed on the declared architecture/OS matrix. |
| User experience | At least five new users complete setup and a simple remap without developer assistance; each blocking observation becomes a tracked fix. |
| Performance | Published measurements meet agreed budgets or document an explicitly accepted exception; no callback timeout or uncontrolled queue growth under the test workload. |
| Release evidence | Support matrix, known issues, changelog, signed artifacts, diagnostic symbols, checksums, and rollback instructions are available for the exact tested build. |

## 9. Recommended delivery order

| Milestone | Work | Exit condition |
| --- | --- | --- |
| **1. Stop losing input or state** | DX-01, IPC buffer fix, P0-01 through P0-05, P0-10 | Isolated daemon tests reproduce the important failures; configuration and device lifetimes are safe; takeover/recovery and held input work. |
| **2. Make advertised behavior real** | P0-06 through P0-09, P0-11, remaining build/test repairs | Slow actions cannot block input; settings are acknowledged; scroll/actions work end to end; health output is truthful. |
| **3. Ship a dependable mouse beta** | P1 setup, diagnostics, recovery, signing/update work; performance baseline; hardware matrix | A small named device set passes lifecycle and soak gates; new users can install and recover without source-code help. |
| **4. Improve daily use and contributions** | Optional UI, mapping preview, profile/config UX, documentation, contributor workflow | Common configuration needs require no TOML editing; contributors can safely reproduce and fix bugs. |
| **5. Expand keyboards and platforms** | P2 capabilities in separate releases | Each added capability has verified device support and preserves the same input safety and release gates. |

Do not bundle every item into one rewrite. For each change: record the failure or missing behavior, add a meaningful regression, implement the smallest coherent fix, run the appropriate checks, and verify the relevant hardware path before advertising support. The first release should make a limited, explicit set of features dependable; subsequent releases can expand that set.
