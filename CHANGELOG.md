# Changelog

## v1.0.2

- Consolidate API, lifecycle, diagnostics, and build documentation.
- Correct queue-statistics documentation and include current capability fields.
- Extract version-specific release notes from the changelog.
- Add project metadata. Native API behavior is unchanged.

Changes and compatibility notes for each version.

## v1.0.1

This update installs the bridge under `_UE4SSLuaEventBridge` so UE4SS loads it
before ordinary consumer mods. The Lua and native API names remain unchanged.

On boot, the bridge detects a sibling legacy `UE4SSLuaEventBridge` folder. If
that folder has no `deprecated.txt`, the bridge removes its `enabled.txt` marker
and writes `deprecated.txt` containing `_UE4SSLuaEventBridge`.
Existing deprecation markers are left unchanged, so the migration is idempotent.

## v1.0.0

UE4SSLuaEventBridge provides a native Enhanced Input event boundary for UE4SS Lua mods. Unreal recognizes action phases and Tap/Hold triggers, while Lua receives copied event data outside the native input-dispatch stack.

### Reliability and lifecycle

- Add target-scoped delivery-fault handling for queue rejection, allocation failure, and Lua callback failure. Faulted targets stop delivery and notify the consumer once so held state can fail closed.
- Strengthen subscription teardown, stopped-session isolation, component-destruction handling, late-callback suppression, cleanup retry ownership, and repeated Lua reload behavior.
- Add native lifecycle tests, simulated engine-boundary integration tests, and AddressSanitizer coverage for concurrent shutdown and queue ownership.
- Retain the DLL conservatively if Unreal may still own a cloned native binding, preventing calls through an unloaded vtable.

### Bounded delivery

- Bound the producer event queue and diagnostic trace queue, expose dispatch statistics, and reject overload explicitly instead of allowing silent unbounded growth.
- Preserve FIFO order across budgeted dispatch passes. Defaults allow 256 live callback attempts or 2 ms of dispatch work per eligible pass.
- Keep native input delegates Lua-free and retain the configurable 20 Hz queue-dispatch schedule.

### Packaging

- Unify runtime, build, archive, and release metadata around one product version.
- Add package manifests and checksums for verifying release contents.
- Preserve installed settings and user-controlled enablement during deployment.

API compatibility remains version 4. This build targets Windows x64, Unreal Engine 5.5, and UE4SS commit `97b7e501`.

Helper scopes must still be closed explicitly on the Unreal game thread before Lua shutdown. An off-thread Lua stop deactivates native callbacks safely but cannot remove helper-created mapping contexts after the Lua state is destroyed. Primitive `BindAction` consumers that own their actions and contexts are unaffected by this helper limitation.

## v0.3.4-rc.3

- Add eight offline lifecycle integration cases, including 100 fresh Lua sessions, late callbacks, cleanup retries, partial removal failures, and simulated component/subsystem destruction.
- Preserve user-controlled enablement, including an absent enabled.txt on fresh installations.

API compatibility remains 4. The native input implementation is unchanged apart from the product version. The archive includes enabled.txt; existing installation enablement remains user-controlled.

The lifecycle tests exercise the production Lua helper against simulated engine/native boundaries. They do not establish Unreal garbage-collection safety, native shutdown race safety, or resolve off-thread generated-context cleanup. No new gameplay acceptance or performance claim is made.

## v0.3.4-rc.2

### Changes

- Scheduled event dispatch defaults to 20 passes per second, with configurable rate, event-count and time budgets. Pending events retain FIFO order across passes.
- Bounded event and trace queues with explicit overflow rejection reporting and on-demand `GetDispatchStats()` counters. Defaults allow 256 events or 2 ms per pass and 65,536 queued producer events; slow callbacks cannot be preempted.
- On-demand binding snapshots, stronger binding identity checks, and improved lifecycle regression coverage.
- Consistent product/version metadata, package contents, and checksums.

### Compatibility and validation

Windows x64, Unreal Engine 5.5 and UE4SS commit `97b7e501` only. API version remains 4.

Automated native, Lua, and packaging checks accompany this release. No host-game acceptance run was performed, and no performance advantage is established.

### Known limitation

When a Lua mod stops off the game thread, its callbacks are deactivated but helper-generated mapping contexts can remain installed. Close helper input scopes on the game thread before stopping/reloading the owning Lua mod where possible. Automatic off-thread context removal remains unresolved; this prerelease does not claim to fix it.

The ZIP contains only `UE4SSLuaEventBridge/enabled.txt` and `UE4SSLuaEventBridge/dlls/main.dll`. Its SHA-256 checksum and source-commit/file-hash manifest are separate release assets.

## v0.3.3

### Changes since v0.3.2

- Added descriptive primitive failure returns while preserving the existing
  successful return values and `UnbindAll` completion flag.
- Added opt-in per-`OpenInput` debug tracing for generated Enhanced Input
  phases, native delegate entry, queue decisions, dequeue, and Lua callback
  completion or failure.
- Added stable helper scope/binding IDs, atomic event sequences, OS thread IDs,
  game-thread state, and rejection reasons to trace lines.
- Added `sequence` to primitive callback payloads and `scope_id`/`binding_id`
  to helper callback payloads.
- Delayed mapping-context activation until native subscriptions are ready and
  made debug observer cleanup part of the logical helper binding lifecycle.
- Raised the capability API to 4 and extended `GetCapabilities()` with
  `detailed_errors` and `debug_tracing`.

### Compatibility

- UE4SS 3.0.1 Beta #0, commit `97b7e501`
- Unreal Engine 5.5
- Windows x64

Debug tracing is disabled by default. It covers the generated Enhanced Input
action through native-to-Lua delivery; raw physical-key detection remains the
responsibility of the consuming mod. Use `event_seq` to correlate every trace
stage produced for one native action event.

## v0.3.2

### Changes since v0.2.10

- Hardened ABI validation, callback dispatch, teardown, native-binding ownership,
  and DLL lifetime behavior.
- Added `Helpers.OpenInput`, direct key binding with Enhanced Input Tap/Hold
  triggers, developer documentation, and a complete F10 sample mod.
- Rejected helper mutations outside the Unreal game thread.
- Synchronized Lua session aliases and retained inert stopped-session storage
  while queued or in-flight work may still reference it.
- Removed the unused generic event-registry scaffold from the source tree.

### Compatibility

- UE4SS 3.0.1 Beta #0, commit `97b7e501`
- Unreal Engine 5.5
- Windows x64

The release contains the installable mod ZIP and its SHA-256 checksum.
