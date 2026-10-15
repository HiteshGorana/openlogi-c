# OpenLogi: latest improvement review

Reviewed **2026-10-15** against commit **`601764577c1d0ab659aad8dba1b441364f9c3a30`** (`0.1.7-c`).

OpenLogi's next milestone should be dependable daily mouse use on a small, verified macOS/device matrix. The highest-value work is to preserve native input, make press/release handling correct, serialize runtime state, and make configuration and status reflect what actually happens. Broader features and a richer UI should build on those guarantees.

This review covers runtime reliability, hardware protocols, actions, configuration, IPC, security, installation, app UX, performance, tests, documentation, maintainability, and platform expansion. It updates the assessment in [improvements.md](improvements.md), which reviewed an older commit. **This document proposes work; it does not implement the fixes.**

## 1. Evidence and validation

Findings are labeled **Reproduced** when exercised by an isolated probe, **Source-confirmed** when established by the current implementation, and **Needs hardware validation** for device-dependent effects. Source-confirmed concurrency hazards were not reproduced as crashes.

| Check performed | Result and limits |
| --- | --- |
| Clean build of all tools and test binaries in `/tmp/openlogi-latest-review` | Passed with Apple Clang 21, C99, `-Wall -Wextra -Werror -pedantic`; no compiler warnings. Host reported arm64, macOS 27.0.1. |
| Selected existing component suites | **19/19 passed.** `test_config` and `test_ipc_server` initially failed environment-dependent foreground-app/socket checks inside the sandbox; both passed when rerun outside it. |
| Separate ASan/UBSan build, including vendor instrumentation | **17/17 selected suites passed**, plus the temporary review probe. No sanitizer diagnostics in these runs. This was not the full `make asan` target and does not establish leak freedom or race freedom. |
| Offline installer and release-publishing tests | `sh tests/test_install.sh` and `sh tests/test_release.sh` passed. They do not establish real DMG launch, permission continuity, or hardware behavior. |
| Temporary C probe against production parser, dispatcher, and daemon callback code | Confirmed workflow delay misparsing, action-name mismatches, SmartShift inconsistency, premature button clicks, numeric RPC truncation, and false offline-setting success. No live input was injected. |
| Structure sizes on this build | `sizeof(openlogi_action_t) = 136`; `sizeof(openlogi_config_t) = 606,288` bytes. The action meets the documented size limit; the configuration exceeds the guide's under-500-KB claim. |
| Test registration audit | **21 C suites / 116 registered test cases**; the runner still advertises 114. Registration count is separate from tests executed during this review. |

The normal runs excluded `test_cli` and `test_device_probe`: the former includes real service-stop/doctor/discovery commands, and the latter enumerates real hardware. Sanitized runs additionally excluded the two environment-dependent suites. No device settings, live daemon lifecycle, login configuration, permission grants, or release assets were intentionally changed. Physical reconnect/wake behavior, latency, RSS, energy use, and release binary size remain unmeasured.

Build logs and temporary probes were kept under `/tmp/openlogi-latest-review*`; these are local review artifacts, not committed regression tests.

## 2. Recent progress to preserve

Several claims in the older plan are no longer accurate. Keep these improvements and extend their coverage:

| Area | Present in this revision | Remaining gap |
| --- | --- | --- |
| Transactional parsing | Parsing uses a temporary configuration; invalid TOML preserves the previous configuration. | Publication/freeing is unsynchronized with input readers; semantic errors are often accepted. |
| Workflow ownership | `config_free()` exists; queued workflows are deep-copied. | Parser ownership contracts, allocation failures, cancellation, and active readers need stronger guarantees. |
| Slow actions | A bounded worker queue exists for shell commands, scripts, applications, sleep, and workflows. | Running jobs have no deadline; shutdown drains work before undiverting hardware; enqueue failure is ignored. |
| Synthetic input | Generated events are marked and bypass interception. | Held shortcuts, Unicode, and several advertised actions remain incomplete. |
| Device lifecycle | Hotplug callbacks, removal untracking, reapply planning, and standby transitions are connected. | In-flight handle lifetimes, per-device routing, wake recovery, and receiver slots remain incomplete. |
| Scroll inversion | `invert_scroll` is parsed; discrete vertical scroll is inverted in place; phase/continuous events pass through. | Firmware encoding is inconsistent with the upstream implementation; other scroll controls remain disconnected. |
| Permissions | Status queries Accessibility and Input Monitoring. | Permission reporting, hook readiness, device readiness, and actual capture health are still conflated in places. |
| IPC and CI | Long client requests allocate enough storage; macOS dispatch releases the client lock before callbacks; vendor sanitizer flags are passed. | Stream deadlines/backpressure, semantic validation, test isolation, and truthful reporting remain open. |
| Distribution | Unified executable, app bundle, DMG packaging, checksum verification, rollback, and dual-architecture release jobs exist. | Bundle dispatch, full-install rollback, readiness, signing identity, and DMG verification need work. |

## 3. Priorities

**P0** means input safety, unintended input, crashes, or unsafe shutdown: fix before a stable release. **P1** means correctness and a dependable user experience: fix before recommending broad use. **P2** means capabilities and longer-term expansion. Effort is relative: **S** is a focused change, **M** crosses several modules, **L** needs architectural work and integration coverage; these are not calendar estimates.

| ID | Priority | Improvement | Effort |
| --- | --- | --- | --- |
| R01 | P0 | Own configuration, device handles, and transactions safely | L |
| R02 | P0 | Divert only supported controls with a usable replacement | M |
| R03 | P0 | Correct per-control press/release and cancellation | M |
| R04 | P0 | Reject malformed workflows; fix documented numeric delays | S–M |
| R05 | P0 | Restore input before bounded shutdown; verify process identity | M |
| R06 | P1 | Unify SmartShift semantics and truthful hardware outcomes | M |
| R07 | P1 | Correct wheel protocol and complete scroll behavior | M |
| R08 | P1 | Preserve source-device identity throughout dispatch | L |
| R09 | P1 | Separate app launch from explicit CLI/service commands | M |
| R10 | P1 | Finish IPC framing, deadlines, and validation | M |
| R11 | P1 | Validate and save configuration reliably | M |
| R12 | P1 | Make the action catalog match executable behavior | M |
| R13 | P1 | Remove unintended shell interpretation and harden local files | M |
| R14 | P0/P1 | Prevent capture in standby; complete wake and permission recovery | M |
| R15 | P1 | Ship one coherent, verified installation | M–L |
| R16 | P1 | Test the actual daemon paths without touching the desktop | L |
| R17 | P1 | Make documentation and diagnostics trustworthy | S–M |
| R18 | P1 | Establish performance budgets and remove unnecessary work | M |
| R19 | P1/P2 | Improve setup, configuration, accessibility, and recovery UX | M–L |
| R20 | P1 | Simplify module boundaries and developer workflows | M |
| R21 | P2 | Expand hardware and platform support deliberately | L |
| R22 | P1/P2 | Improve project governance, dependency records, and support | S–M |

## 4. Input safety and runtime correctness

### R01 — Establish runtime ownership and safe snapshot lifetimes

**Source-confirmed.** [daemon_reload_config()](src/daemon/main.c#L872) frees the live configuration and assigns its replacement from the IPC/watcher thread while the hook thread reads bindings and workflows. The hook, main loop, and IPC callbacks also share `g_button_rt`, inventory, and device handles without a consistent synchronization policy. Transactional parsing alone does not make this publication safe.

[hid_transact()](src/platform/macos/hid_iokit.c#L543) uses one mutable pending-request slot per device. Hardware actions can call it from the input hook or IPC thread. Its nested run-loop pump can deliver [on_device_removal()](src/platform/macos/hid_iokit.c#L273), which frees the handle while the transaction still holds a local pointer. Hotplug untracking improves cleanup but does not protect an outstanding transaction.

**Improve:** give device state and HID operations one owner; enqueue commands to that owner; use connection generations and explicit cancellation. Replace synchronous nested waits with scheduled request/response completion where practical. Publish immutable binding snapshots with a documented lifetime, and retain the selected action for the duration of a press/job. Synchronize shared flags with C99-compatible mechanisms; `volatile` is insufficient.

**Acceptance:** reload, button input, concurrent RPC writes, and disconnect-during-transaction tests run through production runtime code without stale reads, mixed responses, deadlocks, or outstanding references after teardown. Add a separate race-testing build; [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) specifically detects data races.

### R02 — Make hardware capture conditional, reversible, and acknowledged

**Source-confirmed.** [daemon_apply_diversion()](src/daemon/main.c#L434) falls back to diverting Gesture and ModeShift when the resolved plan is empty. Secondary collections divert both unconditionally. It ignores write results and only adds captures on reload; removed bindings are not explicitly undiverted. [capture_plan.c](src/core/capture_plan.c) combines all app profiles, does not check whether an action is usable, and omits an ordinary `DpiToggle` binding from its explicit mapping.

This can suppress native input with an empty configuration, after removing a binding, or in an application with no replacement action. The 32-entry [diversion registry](src/hid/hidpp_diversion.c#L286) also silently fails to track additional captures after the firmware write succeeds.

**Improve:** resolve supported CIDs from device capabilities and the effective profile; require working delivery before capture. Compute and apply added/removed controls, reserve tracking capacity before writes, verify acknowledgements, and roll back partial failure. Define native fallback for app-only bindings and restore prior reporting state where possible.

**Acceptance:** empty config, unknown action, app-profile exit, removed mapping, full registry, denied permission, and failed write all preserve or restore native behavior. Tests must inspect firmware command sequences and effective capture state.

### R03 — Decode button snapshots into real transitions

**Reproduced.** [on_hid_report_event()](src/daemon/main.c#L321) calls `button_runtime_down()` for Back/Forward/Middle, then enters the “all released” branch whenever neither Gesture nor ModeShift is down. A Back-only **down** report therefore produces a click immediately and leaves no held state. Sending the same down report again produces another click, without a physical release.

Other lifecycle gaps remain: every down passes `has_long_press=false`; the callback handles only triggered events; profile lookup happens at trigger time; app changes cancel all presses; drag events do not complete the suppressed button sequence. Left/right mappings exist in the model but the event-tap mask does not capture those buttons.

**Improve:** maintain the previous pressed-CID set per device and calculate set differences. Bind the selected action/profile at press time, process started/triggered/ended/canceled events, and implement held-key ownership. Choose an explicit policy for app changes during a hold. Capture or reject each advertised physical input consistently.

**Acceptance:** repeated snapshots produce no extra actions; two simultaneous controls release independently; a held shortcut stays down until release; cancellation releases only OpenLogi-owned input; remapped drags have coherent down/drag/up behavior.

### R04 — Make workflow parsing fail safely

**Reproduced.** [parse_button_bindings_table()](src/core/config.c#L633) recognizes `Delay` only as a table containing `millis`. The documented `{ Delay = 120 }` is accepted but leaves a zero-initialized step. Because `WORKFLOW_STEP_PRESS_KEY` is enum value zero, the executor treats it as keycode zero instead of a delay. Unknown steps and invalid shortcuts can fall through similarly. The probe inspected the resulting step; it did not inject the key.

**Improve:** support one documented syntax, or deliberately support both forms; reject unknown steps, invalid shortcuts, ambiguous action objects, allocation failure, out-of-range delays, and excessive step counts. Do not truncate executable commands or workflows silently. Check delay conversion before multiplying milliseconds by 1,000.

**Acceptance:** every documentation workflow parses into the expected sequence. Invalid input rejects the entire reload, preserves the previous snapshot, identifies the offending step, and never creates a default executable action.

### R05 — Restore hardware before waiting for jobs; stop only the intended process

**Source-confirmed.** [action_queue_shutdown()](src/core/action_queue.c#L191) joins a worker that continues draining queued actions, and [daemon shutdown](src/daemon/main.c#L1191) does this before undiverting hardware. A hanging `system()` command can delay restoration indefinitely. Clearing pending actions does not cancel the running job. Queue rejection is discarded by the dispatcher.

[cmd_stop()](src/cli/openlogictl.c#L2104) trusts a PID from a file and escalates to `SIGKILL` after a short wait. It lacks the IPC/PID verification already used by `uninstall_stop_daemon()`. A stale PID can target another process; forced termination bypasses firmware cleanup. App-dialog stop/uninstall also announces restoration without waiting for confirmation.

**Improve:** stop input producers, cancel scheduled work, restore hardware and owned keys, then reap jobs with deadlines. Spawn child process groups so timeout/cancellation can cover descendants. Report queue-full and failed-action outcomes. Reuse verified daemon identity for stop/restart/uninstall and distinguish confirmed restoration from timeout. Check initialization results and unwind partial startup.

**Acceptance:** termination during a long workflow or hung child restores accessible hardware within a defined budget. Stale PID files never signal unrelated processes. Ordinary stop does not claim hardware recovery merely because a process exited. Crash/forced-exit recovery is tested separately.

## 5. Hardware settings, scrolling, and device coverage

### R06 — Use one hardware vocabulary and report confirmed settings

**Reproduced.** [device.h](include/device.h#L20) defines free-spin as `1` and ratchet as `2`, while [hires_wheel.h](include/hires_wheel.h#L20), config parsing, CLI, and IPC use the reverse names. The probe parsed `mode = "ratchet"` as `1` while the hardware enum is `2`. The `USAGE.md` SmartShift example also produces threshold `0`: the parser expects integer `auto_disengage`, while the example supplies `threshold = 16` and boolean `auto_disengage`.

The daemon now checks writes when a handle/feature is available, but [daemon_set_dpi()](src/daemon/main.c#L800) and its SmartShift counterpart still return success and mutate the cache when that handle/feature is absent. The offline/null-handle DPI case was reproduced. `hidpp_read_dpi()` can return true after unsuccessful reads and only partially interprets the sensor's supported values.

**Improve:** centralize semantic enums and feature-specific wire conversion; remove colliding macros. Preserve capabilities, supported DPI values, freshness, last error, and requested/applied state. Reject offline/unsupported writes; validate ranges before narrowing; acknowledge or read back successful settings. Apply persisted changes on reload as well as connect, with explicit live-only versus saved behavior.

**Acceptance:** CLI, TOML, IPC, inventory labels, mock packets, and physical wheel state agree. A missing device or feature cannot produce success. Failed writes retain the last confirmed value.

### R07 — Correct the wheel protocol before extending scroll options

**Source-confirmed discrepancy; needs hardware validation.** [hires_wheel_build_mode_request()](src/core/hires_wheel.c#L10) uses function index `1` and inversion bit `0`. The [upstream Linux HID++ driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-logitech-hidpp.c) uses wire command `0x20` for setting 0x2121 wheel mode, inversion bit `2`, resolution bit `1`, and HID++ target bit `0`. OpenLogi packs the function index into the high nibble, so this corresponds to function index `2`, not `1`. Existing wheel tests assert the local constants and do not independently validate the protocol.

The daemon ignores the wheel transaction result. It also enables software inversion from the same preference, so correcting firmware inversion requires choosing one inversion owner to avoid reversing twice. Current phase filtering protects trackpad gestures heuristically; it is not physical-device attribution.

The CLI advertises resolution, smoothing, vertical sensitivity, and thumbwheel sensitivity, but [config_device_t](include/config.h#L64) stores only `invert_scroll`; the daemon does not call the scroll pipeline to implement the other controls.

**Improve:** verify feature/version-specific payloads with independent fixtures and hardware traces; retain prior wheel mode and confirm restoration. Choose hardware or software inversion per device. Implement each advertised setting end to end or return “unsupported.” Preserve fractional deltas, horizontal direction, phases, and momentum without altering unrelated devices.

**Acceptance:** test low/high-resolution wheels, a continuous-scroll mouse, and a trackpad together. Inversion is applied exactly once and survives reload/reconnect. Unsupported controls are visibly unavailable rather than silently saved.

### R08 — Carry source identity through the complete action path

**Source-confirmed.** The HID callback finds a matching feature index but loses the device identity before dispatch. Binding resolution passes `NULL` for the device key; button/gesture state is global; CycleDpi and SmartShift actions iterate across devices. Config fallback often chooses the first entry, and hardware-setting lookup can accept `default` before a later exact match. Removal falls back to VID/PID, which cannot distinguish identical models without serials.

**Improve:** introduce a stable device identifier plus connection generation in input, gesture, press, job, and RPC records. Use consistent precedence: exact device, explicit default, then documented fallback. Address receiver slots independently; probing currently uses `0xFF` rather than enumerating paired slots. Treat multiple HID collections as interfaces of a physical device using explicit identity rules.

**Acceptance:** two identical mice maintain independent settings and held inputs. One disconnect does not cancel the other's work. A gesture changes only its source device. Document any device-scoping limitation of the macOS event tap until attribution is implemented.

## 6. App lifecycle, configuration, actions, and IPC

### R09 — Fix bundled command dispatch and unify lifecycle operations

**Source-confirmed.** [openlogi.c](src/cli/openlogi.c#L306) treats any `openlogi` executable path containing `.app/Contents/MacOS/` as a GUI launch before examining explicit subcommands. Thus invoking the bundled `openlogi --help`, `status`, or `daemon` enters the app-launch handler. The generated LaunchAgent specifically points to that bundled executable with `daemon` as an argument. Background service launch can therefore follow interactive setup logic.

There are separate lifecycle implementations in the app dialog, CLI, installer, Makefile, and plist template. They differ on `KeepAlive`, paths, verification, and removal scope. The app path writes a plist but does not bootstrap it immediately; it also treats an existing plist as sufficient even if its target has moved. CLI uninstall removes sibling binaries; app uninstall moves a selected bundle; neither represents one complete installation consistently.

**Improve:** process explicit commands before GUI detection; reserve GUI launch for no-argument/Finder activation. Use one tested lifecycle service for start/stop/restart/uninstall. Record the selected installation, respect its custom paths, repair stale startup targets, and avoid silently choosing an unrelated copy in `/Applications`.

**Acceptance:** the same explicit command behaves identically from the CLI prefix and app bundle. Login startup is noninteractive. Stop remains stopped according to a documented policy. Uninstall previews and removes exactly the intended installation while preserving configuration by default.

### R10 — Finish the JSON-RPC transport and contract

**Source-confirmed; numeric validation reproduced.** [ipc_client.c](src/ipc/ipc_client.c#L55) polls once, then reads a blocking stream one byte at a time without a full-message deadline or size ceiling. A peer can send one byte and stall. Server replies and broadcasts ignore partial writes and `EAGAIN`; responses can be truncated under backpressure. Subscription detection uses a substring instead of the parsed method, and client calls do not match response IDs or separate interleaved notifications.

The dispatcher does not require `jsonrpc = "2.0"`, responds to notifications, and narrows DPI/mode/threshold values before validating their range. A direct probe with DPI `65537` and no `jsonrpc` field returned success with DPI `1`. Notifications should receive no response under the [JSON-RPC 2.0 specification](https://www.jsonrpc.org/specification).

**Improve:** bounded buffered reads, monotonic end-to-end deadlines, partial-write queues, writable readiness, disconnect handling, and a slow-subscriber policy. Validate envelope, parameter types, device keys, enums, and ranges before execution. Implement batch behavior if claiming full JSON-RPC 2.0 compatibility, or explicitly describe the supported subset. Use JSON builders for request/event payloads and correlate replies by ID.

**Acceptance:** fragmented, oversized, malformed, notification-only, mixed event/reply, stalled, and slow-reader cases remain bounded. Invalid values never reach hardware. One client cannot block input or monopolize the IPC loop.

### R11 — Treat configuration as a validated, durable user document

**Reproduced/source-confirmed.** A probe containing schema `999` and `Back = "MisspelledAction"` parsed successfully, yielding `ACTION_NONE`. Unknown keys/buttons, unsupported shapes, limits, and long strings are often ignored or truncated. `enabled` is initialized true rather than parsed. The watcher can claim success with no file descriptor for an initially missing file, and replacement gets only one reopen attempt.

[toml_update_key()](src/cli/openlogictl.c#L485) identifies sections by substring, so `[devices.default.bindings]` can be mistaken for `[devices.default]`. It does not check every write/close result or reparse the completed file before rename. `toml_read_key()` is not section-aware. [cmd_reload_silent()](src/cli/openlogictl.c#L423) accepts any returned JSON document as successful reload, including a negative result.

**Improve:** one schema validator and one configuration service shared by CLI/UI/daemon. Provide proposed `config validate`, `config explain`, and effective-state/diff commands. Reject unsupported schema versions and semantic errors with a field path. Use safe temporary files, checked writes, validation, concurrent-edit detection, and atomic replacement. Watch the parent directory so creation and delayed replacement recover. Document parse-into-empty versus replace-existing ownership.

**Acceptance:** misspelled or unsupported values cannot silently disable a working binding; CLI changes affect only the intended device/section; save failure preserves the original; “reloaded” requires the daemon's positive acknowledgement.

### R12 — Make advertised actions executable and test their effects

**Reproduced/source-confirmed.** The parser rejects documented names `DoubleClick`, `TripleClick`, `BrowserNewTab`, `BrowserRefresh`, and `MediaPlayPause`. Some alternatives exist (`NewTab`, `ReloadPage`, `PlayPause`), but docs and runtime do not share one catalog. `ShowActionsRing` parses yet has no execution path. The `hold` shortcut flag is not honored by the injector. `inject_text()` widens UTF-8 bytes individually instead of decoding Unicode. Application IDs are passed as file operands to `open`, without bundle-ID handling.

Nested parameterized gesture actions in `USAGE.md` are also unsupported by the string-only direction parser. `ThumbWheelUp`/`ThumbWheelDown` differ from the parser's `ThumbWheelScrollUp`/`ThumbWheelScrollDown`. `SetDpiPreset` is an index in code, while parts of the architecture guide describe a DPI value.

**Improve:** centralize canonical action/button names, aliases, argument schemas, execution support, platform support, and help metadata. Generate docs and CLI lists from it. Implement full hold/release and multi-click semantics, UTF-8 to UTF-16 conversion, bundle-ID launch, and shortcut-layout handling. Expose unimplemented actions as unavailable.

**Acceptance:** every advertised name parses and reaches an observable result in a recording backend. Cover Unicode/emoji, modifier-only holds, physical modifiers already held, multi-click counts, unavailable OS actions, and parameterized gesture directions.

### R13 — Keep data out of shell syntax and secure local state

**Source-confirmed.** [inject_macos.c](src/inject/inject_macos.c#L328) constructs shell commands around application paths and AppleScript text. Quotes and shell metacharacters can change execution. [openlogi.c](src/cli/openlogi.c#L191) interpolates a hardware product name into a shell/AppleScript dialog. These inputs should remain data; intentional `RunShellCommand` is a distinct feature.

**Improve:** launch applications and scripts with explicit argument vectors or native APIs; use `open -b` for bundle IDs; pass script text without a shell wrapper. Bound child lifetime and capture useful failure status. Treat imported executable configuration as something users can inspect before enabling it.

Use user-private runtime/log directories, checked path lengths, safe creation flags, and consistent close-on-exec descriptors. Reject a UNIX socket path that does not fit `sockaddr_un.sun_path`; currently it can be silently truncated. Validate directory/file ownership where relevant and check `chmod`/creation failures. Keep the lock file's inode stable: [single_instance_release()](src/core/single_instance.c#L128) unlocks/closes before unlinking, which permits a competing process to lock the old inode before another process creates a new one. Consolidate the inconsistent fallback lock paths.

**Acceptance:** quoted paths/device names/scripts preserve their literal meaning; oversized paths fail clearly; concurrent starts/stops cannot create two independently locked daemon instances. Diagnostics omit script/text payloads, serials, and app history by default.

### R14 — Make standby and recovery match the declared state

**Source-confirmed.** The [standby branch](src/daemon/main.c#L1146) can start the event hook with no usable Logitech device. Since reconciliation returns early for unchanged standby, the hook may remain active and apply global scroll/button mappings to other devices. Runtime state can also become `active` after hook initialization fails.

Session persistence writes a constant `"openlogi-session"` and the current PID; the daemon never calls `armed_session_holds()` to recover a previous session. Wake reapply planning exists, but the production call passes `false` for wake and no system wake observer is installed. Battery values are populated during probing, without a normal refresh path.

**Improve:** represent waiting-for-device, permission-blocked, active, paused, degraded, and stopping states explicitly. Reconcile actual subsystem state even without a state-enum transition. Observe sleep/wake, cancel stale work, revalidate capabilities, and refresh state conservatively. Use a real boot/session identity only if it participates in a tested recovery design; otherwise remove the implied recovery guarantee.

**Acceptance:** no-device standby does not intercept unrelated input; permission grants/revocations recover safely; pause survives the intended lifecycle; reconnect/wake restores only confirmed settings; battery/status have freshness indicators.

## 7. Distribution, testing, and documentation

### R15 — Make the whole installation atomic and verifiable

Preserve the existing archive verification, staged binary replacement, rollback tests, and draft-release publishing. Extend them to the current app-based installation:

- Track CLI executable, aliases, bundle, startup registration, configuration migration, and version as one installation transaction. Current rollback tracks selected binary names but not all alias/bundle changes; service restart can prefer the newly copied bundle after CLI rollback.
- Wait for readiness from the expected version, executable path, and PID. A successful daemon fork or `launchctl bootstrap` is not a readiness acknowledgement.
- Test unified-binary archives and DMGs explicitly. Installer fixtures still generate the legacy three-binary layout. Release tests use placeholder archives and do not exercise the real app launcher.
- Require both DMGs when advertising them. [publish_release.sh](scripts/publish_release.sh) currently requires and verifies tarballs, then uploads whichever DMGs happen to exist; DMG checksums are not included in the published `SHA256SUMS`.
- Generate binary version, `Info.plist` version/build number, release notes, and package metadata from one version source. The plist currently has a hardcoded short version and build number `1`.
- Use stable Developer ID signing and notarization for public macOS distribution, verify the final packaged bundle, and stop re-signing installed artifacts ad hoc. Ad-hoc signing is present today and is not the Developer ID signing used in Apple's [distribution guidance](https://developer.apple.com/developer-id/). Follow Apple's [notarization workflow](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution) and verify permission continuity across updates on real machines.
- Retain symbols separately, pin release automation dependencies, and produce checksums/provenance for every advertised artifact. Make packaging repeatable without leftovers from a previous DMG staging directory.

**Acceptance:** fresh CLI/DMG install, bundle relocation, update, interruption, failed readiness, full rollback, login startup, and uninstall pass on both advertised architectures. No test relies on an existing development installation.

### R16 — Test production behavior with isolated dependencies

The passing component checks are useful, but `test_lightweight_standby` tests helper state calculations rather than `daemon_reconcile_runtime_state()`. The temporary Back-button probe exposed a defect absent from those helper tests. Injection dry-run mode avoids side effects but cannot verify the actual output sequence.

**Improve:** extract the daemon runtime behind fake transport, recording injector, fake app/permission/service adapters, and an injectable clock. Drive real report → device → profile → lifecycle → output behavior. Move physical enumeration, real injection, permissions, service management, and GUI launch checks into explicit integration/hardware jobs.

Add these regressions first:

| Layer | Required scenarios |
| --- | --- |
| Runtime integration | Repeated/overlapping HID button snapshots; device-scoped actions; app switch during press; removed binding; no-device standby; permission loss; reload while a workflow is read. |
| Hardware transport | Disconnect during request, late/duplicate/error response, overlapping requests, receiver slots, partial arming rollback, independent wheel packet fixtures. |
| Config/action | Every documented example/name, unknown schema/action, invalid workflow step, excessive values, Unicode, full queues, canceled jobs, absent/replaced config. |
| IPC/process | Partial writes/reads, stalled peer, notification semantics, response ID matching, startup failure, readiness, hung child, stale PID and lock-inode races. |
| Distribution | Unified archive, both DMGs, bundle command dispatch, alias restoration, app rollback, relocated installation, missing architecture/artifact. |

Add separate ASan/UBSan, TSan, static-analysis, and bounded fuzzing jobs as appropriate. Fuzz packet decoding, TOML/actions, RPC envelopes, and event sequences. Preserve failing inputs as regressions. Sanitizers cover exercised behavior; [ASan](https://clang.llvm.org/docs/AddressSanitizer.html) does not replace the need to test actual daemon paths.

Repair the test runner: derive counts from execution, propagate `BIN_DIR`, handle nonverbose failures under `set -e`, retain logs, and show sanitizer/leak claims only when verified. Add header dependencies (`-MMD -MP`) and separate normal/sanitizer/release outputs; the current shared outputs can reuse incompatible objects after flag changes.

**Acceptance:** standard tests require no physical device or desktop permissions, change no real services, and catch the reproduced defects. A failing nonverbose test yields an accurate summary and nonzero exit status. CI tests the supported architecture/OS matrix, not only a moving `macos-latest` job.

### R17 — Align documentation and diagnostics with tested behavior

Treat [README.md](README.md), [USAGE.md](USAGE.md), [ACTIONS.md](ACTIONS.md), [AGENTS.md](AGENTS.md), CLI help, and the action metadata as one product contract.

- Correct the workflow delay, SmartShift fields, action aliases, preset-index meaning, gesture table syntax, and thumbwheel examples identified above. Validate examples automatically.
- Publish an implementation matrix: parsed, executed, platform-supported, and hardware-verified are distinct states. Remove blanket “all 53 actions” or feature-parity claims until the matrix supports them.
- Replace contradictory counts and footprint claims with generated counts and dated measurements. The current config size is 606,288 bytes; document or reduce it instead of retaining the under-500-KB assertion.
- Update the older improvement/bug documents with reviewed status and links to tests. The scroll bug note describes completed firmware behavior that still needs the protocol verification in R07.
- Fix `doctor_run_click_test()`: it counts `app_activated` and `hook_activated` as proof of a mouse click. Success must require a relevant source-device/button event.
- Separate proposed read-only `doctor --check` from explicit repair/setup operations. Currently even `--json doctor` may enable a service when disconnected. JSON output and exit codes should have stable schemas and accurately represent failures.
- Expose config path/generation, executable identity, capabilities, capture state, stale hardware values, last reload rejection, transaction timeouts, action failures, and queue drops in status/support output.

**Acceptance:** a new user can follow every starter example successfully on a declared supported device; diagnostics explain the next action without claiming untested functionality.

## 8. Performance, product experience, and long-term structure

### R18 — Measure responsiveness and energy before optimizing further

**Source-confirmed opportunities.** The active main loop wakes every 50 ms, foreground-app polling runs every 100 ms, and input dispatch also queries NSWorkspace synchronously. Hardware actions can perform blocking HID requests from the input callback. Standby still performs periodic scanning and may start the hook. “0.00% CPU” is not established by this architecture.

Cache the frontmost app through notifications, move firmware work and JSON/log emission out of input callbacks, use monotonic clocks instead of `gettimeofday()` for deadlines, and schedule only needed timers. Evaluate [NSWorkspace activation notifications](https://developer.apple.com/documentation/appkit/nsworkspace/didactivateapplicationnotification) for app tracking. Prefer bounded reusable queues after measuring contention; fewer threads alone is not a performance result.

These are **proposed initial targets**, not current measurements:

| Metric | Initial target | Measurement |
| --- | --- | --- |
| Input callback duration | p99 under 1 ms; no blocking hardware/process work | Timestamp callback entry/exit during scrolling, gestures, reloads, and IPC pressure. |
| Simple remap overhead | p95 under 2 ms, p99 under 5 ms | Capture-to-output-enqueue time; exclude deliberate long-press/workflow delay. |
| Standby behavior | No input hook or app polling with no usable target device | Inspect live subsystem state, threads, and wakeups. |
| Idle CPU/energy | Start with average CPU below 0.1% of one core over 10 minutes | Record named hardware, release build, connected/disconnected state, wakeups, and energy impact. |
| Memory stability | No sustained growth across 10,000 reload/job cycles | Record RSS and physical footprint separately, including queue peaks. Establish a realistic baseline first. |
| Reconnect and stop | Initial goal: p95 under 2 seconds after a responsive device appears; ordinary stop under 2 seconds | Separate discovery, probing, confirmed application, input restoration, and child cleanup. |
| Release footprint | Retain the guide's under-400-KB stripped core target, or record an explicit reviewed exception | Build with release settings; measure arm64/x86_64 separately and distinguish signing/bundle overhead. |

Store benchmark scripts and results per release. Do not compare C/Rust/UI footprints without matching workloads, build modes, OS versions, and measurement definitions.

### R19 — Make daily configuration and recovery approachable

The app currently provides a one-shot status dialog and the CLI provides setup/doctor flows. Improve those existing paths before a large UI project:

- Show connected devices, applied settings, permission state, effective profile, and a clear pause/resume control. Distinguish pause-until-resume, stop-now, and disable-at-login.
- Offer capability-aware button assignment, shortcut recording, conflict detection, gesture preview, and a reversible “test this mapping” flow. Show unsupported actions/settings as unavailable with a reason.
- Provide a safe default profile, per-app selection, import/export with validation, restore-defaults, and an explicit config backup/undo path. Use the same backend validation as CLI/RPC.
- Keep setup progress grounded in readiness checks; return users to the exact missing permission or unavailable device step. Avoid clipboard replacement and intrusive notifications during routine diagnostics.
- Make a future native settings window/menu-bar UI an optional IPC client with its own resource budget. Support keyboard navigation, VoiceOver labels, text scaling, and readable status without color alone. For CLI output, honor terminal/nonterminal behavior and `NO_COLOR` consistently.
- Test setup with a handful of new users; record completion rate, time to first successful remap, recovery success, and confusing steps.

**Acceptance:** a new user installs, grants permissions, remaps one supported button, sees it work, pauses it, and restores defaults without editing TOML or consulting source code.

### R20 — Extract boundaries that make changes safer

[main.c](src/daemon/main.c) is 1,218 lines and [openlogictl.c](src/cli/openlogictl.c) is 2,367 lines. Size alone is not a defect, but these files mix state ownership, protocol policy, UI, persistence, service operations, and diagnostics, making the fixes above harder to test.

Extract lifecycle/device management, input routing, configuration publication, settings application, and action scheduling into focused modules. Keep CLI/app presentation separate from shared configuration and service operations. Centralize paths, action metadata, SmartShift conversion, and version metadata. Keep OS-specific shortcut tables and APIs behind platform adapters while preserving host-independent protocol tests.

Document ownership/thread affinity and structured error results in public headers. Check allocation/system-call results and Objective-C autorelease-pool lifetimes on worker paths. Add C99-compatible compile-time size assertions for agreed budgets. Supply a compile database, formatting guidance, debugger recipes, and one documented validation entry point without introducing unnecessary runtime dependencies.

**Acceptance:** a regression can exercise the production behavior through a small interface without including the executable's `main.c` or requiring a live desktop.

### R21 — Expand supported devices and platforms with explicit gates

Keep the current macOS scope clear. Linux service packaging exists, but the tree does not implement Linux HID/input backends; the daemon also has an unguarded `CFTimeInterval` declaration. A Linux service file is not platform support.

First publish a matrix of model, firmware, transport, OS, and verified features. Add receiver-slot discovery, stable multi-device identity, capability querying, battery freshness, coexistence checks, and real wake/reconnect coverage. Then expand advanced gestures, horizontal-wheel bindings, keyboard controls, and haptic features only for verified hardware.

For Linux, first make the platform-neutral parser/protocol/runtime tests build independently, then implement hidraw, evdev/uinput, permission setup, config watching, and service integration with explicit desktop limitations. Treat Windows as a separate backend project. Preserve the C99 core and dependency-light runtime.

**Acceptance:** each advertised feature/platform has a real implementation, automated coverage, and recorded physical-device verification or an explicit experimental label.

### R22 — Make the project easier to adopt, maintain, and support

Add `CONTRIBUTING.md`, issue templates, a security-reporting route, a changelog/release checklist, and compatibility/migration policies. No project-level license file was found in this review; the maintainer should explicitly select/document the project's license and retain vendor notices before encouraging redistribution. Record upstream revisions, licenses, and update procedures for vendored libraries; do not assume vendoring removes maintenance obligations.

Offer a local redacted diagnostic bundle users can review before sharing. Include build/version, OS, transport, capabilities, effective state, bounded error counters, and a minimal reproduction; omit private text/scripts/serials by default. Assign each accepted improvement an owner, regression, and hardware-verification requirement where applicable.

## 9. Delivery sequence and release gates

Avoid a broad rewrite. Deliver small changes with tests that exercise observable outcomes.

| Milestone | Work | Exit condition |
| --- | --- | --- |
| 1. Reproduce and protect input | Recording runtime harness; R03/R04 regressions; R02 capture fallback; R05 restoration; R14 standby capture fix | No premature/duplicate clicks, executable malformed workflow steps, or captures without replacements; standby leaves unrelated input alone; stop restores input before waiting on jobs. |
| 2. Establish ownership | R01 plus device identity foundations from R08 | Reload/disconnect/settings stress passes with defined ownership, canceled stale work, and race checks. |
| 3. Make settings and commands truthful | R06/R07, R09–R14, action/schema metadata | Documented examples work; unavailable features fail clearly; bundled commands and service startup are predictable. |
| 4. Ship a dependable macOS beta | R15–R18, onboarding improvements from R19 | Both architectures install/update/rollback cleanly; diagnostics are accurate; hardware matrix and performance baseline are published. |
| 5. Expand deliberately | R19–R22 and selected advanced features | Optional UI, devices, and platforms pass the same safety/quality gates before being advertised. |

Before a stable release, require:

- Strict builds and isolated component/integration tests, truthful sanitizer results, and regression coverage for every resolved item above.
- Fault-injected reload, disconnect, permission loss, full queues, stalled IPC, and hung child tests that preserve usable input.
- Repeated physical reconnect/sleep/wake tests on each representative transport class, including two simultaneous devices and a trackpad.
- A recorded multi-day hardware soak with no crashes, unexplained input loss, stuck owned keys, or sustained resource growth.
- Fresh install, login launch, update, failed-update rollback, and uninstall of the exact release artifacts on the supported architecture/OS matrix.
- Measured callback latency, CPU/wakeups, memory, reconnect, shutdown, and binary size; record exceptions instead of silently changing targets.
- Documentation generated or checked against supported schema/action metadata, plus explicit known limitations and a tested recovery path.

**Recommended first implementation:** add a recording daemon test harness and fix the reproduced HID button transition and numeric workflow-delay defects. They are concrete, directly affect input, and provide useful integration coverage for the larger ownership and capture fixes.
