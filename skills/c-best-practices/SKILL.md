---
name: c-best-practices
description: Write, refactor, and review C source, headers, and build changes for portable C99, memory safety, reliable error handling, and efficient concurrency. Follow the target repository's architecture and validation requirements.
---

# C best practices

Read the repository's AGENTS.md and relevant build targets first. Respect its language standard, dependencies, architecture, and existing conventions. Keep changes within the requested scope.

## Ownership and resources

- Make ownership explicit: borrowed pointers, transferred ownership, and caller-owned output buffers. Document lifetime and thread-affinity requirements where callers could misunderstand them.
- Pair allocations, descriptors, parsed documents, OS objects, and child processes with cleanup on success, partial initialization failure, and shutdown.
- Initialize handles to invalid values. Use cleanup labels when multiple failure paths release the same resources; release only resources actually acquired.
- Check allocation-size arithmetic for overflow. Allocate with sizeof *pointer and retain the original pointer until realloc succeeds.
- Keep stack use bounded. Avoid input-sized VLAs and large nested structs; put large configuration objects and workflow storage on the heap when needed.
- Deep-copy owned nested data when queued work must outlive its source. Define pending-work behavior during configuration reload and shutdown.

## Bounds and undefined behavior

- Validate lengths, indexes, enum values, and numeric ranges at input boundaries. Use size_t for object sizes and check narrowing and signedness conversions.
- Check snprintf results for failure and truncation. Do not assume strncpy terminates strings. Define whether oversized input is rejected or truncated.
- Decode protocol bytes explicitly using fixed-width unsigned types and documented byte order. Avoid casting packet buffers to structs because alignment, padding, and aliasing can invalidate that approach.
- Guard shifts, integer overflow, division by zero, and pointer arithmetic. Pass unsigned-char values to ctype functions when processing potentially signed char data.
- Distinguish malformed input, absent optional values, and valid zero values. Reject partial numeric parses and unexpected trailing data.

## Interfaces and errors

- Keep headers self-contained with include guards. Make internal helpers static and borrowed read-only inputs const where appropriate.
- Follow the project's error conventions. Check system-call results; preserve errno when cleanup could overwrite it.
- Handle short reads and writes, EOF, incomplete messages, and backpressure. Retry EINTR when appropriate; handle EAGAIN/EWOULDBLOCK in nonblocking paths.
- Bound queues, messages, and retries, and define overflow behavior. Do not silently lose release events or accepted work.
- Keep signal handlers async-signal-safe. Request cleanup through a flag or wakeup mechanism; perform allocation, logging, mutex operations, and hardware teardown in ordinary execution context.

## Threads and event loops

- Identify the owner of mutable state and OS resources. Prefer one event-loop owner with queued commands when this simplifies synchronization.
- Use mutexes or a supported atomic mechanism for shared state. volatile is not thread synchronization. Preserve the selected C standard when choosing primitives.
- Keep input-tap and HID callbacks short. Offload blocking work; avoid sleeps, file parsing, child waits, and blocking I/O in latency-sensitive callbacks.
- Before changing thread count, inspect explicit thread creation and runtime stacks. Share event loops for compatible wait-driven services; preserve isolation for blocking workflows and sensitive input processing.
- Prefer notifications and readiness events over polling when practical. Schedule debounce, retry, and workflow deadlines instead of sleeping on shared loops.
- Do not assume dispatch queues reduce OS thread count. Measure idle and active thread counts, wakeups, resident memory, and input latency.
- Shutdown must stop producers, wake waits, account for queued work, join owned threads, and release resources without holding locks those threads need. Handle partial startup and repeated stop calls.

## Portability and structure

- Follow the repository's selected C standard. Guard platform extensions with portable alternatives or explicit supported-platform boundaries.
- Investigate compiler warnings rather than suppressing them. Keep protocol parsing and state machines independent of host APIs and testable through mocks.
- Keep shared vocabulary and parsing centralized; avoid duplicating action names, button names, configuration semantics, or wire formats.
- Prefer clear state machines and small interfaces over clever macros, hidden global coupling, and unnecessary abstraction.

## Validation

Run relevant builds and tests for changed behavior. Add meaningful regression coverage for new features and defects: boundary values, malformed input, resource cleanup, lifecycle failures, and protocol framing. Avoid tests that merely duplicate implementation details.

Use existing sanitizer targets for memory and lifetime changes. Sanitizers do not prove absence of data races or correctness of unexercised paths. Report checks actually run and remaining gaps.

### OpenLogi requirements

In openlogi-c, AGENTS.md remains the canonical contract:

- Preserve C99 and clean compilation under -Wall -Wextra -Werror -pedantic.
- Keep HID hardware logic in src/hid/ and host APIs behind platform boundaries.
- Register shared button/action vocabulary in include/config.h and src/core/config.c.
- Preserve hardware undiversion on shutdown and failure cleanup while keeping teardown outside unsafe signal-handler execution.
- Preserve responsive input handling and IOKit run-loop servicing. Verify run-loop ownership before moving HID operations between threads.
- Run make test for affected runtime behavior and make asan before any commit or pull request. For release or footprint changes, use make strip and verify the repository's binary-size target.
- Check current configuration limits and test counts in source rather than treating historical documentation as measured evidence.

Finish with a concise account of changes, validation, and material limitations. Do not expand the task into unrelated refactors, commits, publishing, or installations.
