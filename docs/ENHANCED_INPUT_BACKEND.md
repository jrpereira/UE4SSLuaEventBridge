# Enhanced Input backend

## Purpose

Translate native Enhanced Input action events into Lua callbacks without
installing `ProcessEvent` hooks, UObject listeners, scans, or polling. Enhanced
Input remains responsible for trigger evaluation and value generation.

The backend is game-agnostic. A caller supplies both the exact live
`UEnhancedInputComponent` object path and the exact `UInputAction` object path.
The bridge does not discover player controllers or infer gameplay/UI state.

## Pinned compatibility boundary

The native adapter targets:

- UE4SS `3.0.1 Beta #0`, commit `97b7e501`
- Unreal Engine 5.5
- Windows x64 with the MSVC ABI

The verified UE 5.5 layout constants are:

| Type or field | Pinned value |
|---|---:|
| `UInputComponent` size | `0x140` |
| `UEnhancedInputComponent` size | `0x178` |
| action-event binding array offset | `0x140` |
| `UObject::ClassPrivate` offset | `0x10` |
| `FInputActionInstance` size | `0x60` |
| `FEnhancedInputActionEventBinding` size | `0x20` |

The required UE 5.5 virtual order after the virtual destructor is:

1. `Execute`
2. `Clone`
3. `SetShouldFireWithEditorScriptGuard`
4. `IsBoundToObject`
5. `GetUObject`

Declaration order is ABI-significant. Compile-time size checks cover the local
weak pointer, binding base, action-event binding, instance view, unique-pointer
return wrapper, and array view.

## Native binding route

`UEnhancedInputComponent::BindAction` is a template/non-reflected C++ API, so
the bridge appends an owned polymorphic binding to the component's native
action-event array. Before reading or mutating that array it verifies:

- the component still resolves through its weak object reference;
- the component's reflected size is exactly the pinned UE 5.5 size;
- array size/capacity invariants;
- a non-null, pointer-aligned allocation when capacity is non-zero; and
- a bounded capacity before growth.

A mismatch fails closed. All public operations that touch the component or
action require Unreal's game thread.

The pinned UE4SS build's imported `FWeakObjectPtr(UObject*)` construction path
is deliberately not used. The bridge creates and resolves the equivalent
index/serial pair locally using the verified object-array offsets.

## Ownership and event delivery

1. `OpenInputComponent` resolves the exact supplied component once and stores a
   weak reference behind a session-owned target handle.
2. `BindAction` resolves the exact supplied action, attaches the native binding,
   and stores a shared subscription record.
3. Native `Execute` copies the event value into a bridge-owned queue. It never
   re-enters Lua.
4. `on_update` drains the queue and invokes one dispatcher closure in the
   owning Lua state.
5. The dispatcher looks up the individual callback in Lua-owned tables.
6. Unbind, target close, and unbind-all deactivate the subscription before
   detaching its native binding on the game thread.
7. Callback failure and off-thread Lua-stop deactivate immediately; the next
   game-thread bridge operation detaches the inactive binding.

Queued events retain the shared subscription record, but an inactive record is
discarded before its Lua session is dereferenced. If Unreal has already
destroyed a component, weak resolution fails and detach avoids dereferencing
the stale native binding pointer.

Native bindings and any engine-created clones share a dispatch state rather
than retaining a raw backend pointer. The state rejects new events during
shutdown and counts live polymorphic binding objects. If UE4SS unloads the C++
mod off-thread while any such object remains under Unreal ownership, the DLL
adds a process-lifetime module reference. This intentionally trades a bounded
module residency for avoiding a dangling vtable after `FreeLibrary`.

## Deliberate exclusions

- no player-controller or pawn discovery;
- no UObject create/delete listener;
- no `ProcessEvent` hook;
- no background component scanning or polling;
- no automatic rebinding after component replacement; and
- no game-specific UI, pause, or gameplay-state filtering.

The Lua caller owns target selection, rebinding, and unbinding policy.

## Helper layer boundary

Version 0.3.2's `Helpers.OpenInput` layer is shipped in the bridge's embedded
Lua API. It uses UE4SS object construction and ordinary reflected Enhanced
Input calls to create transient mapping contexts, Input Actions, and Tap/Hold
triggers. The ABI-pinned native backend remains responsible only for explicit
component targets, native action-event bindings, queued event delivery, and
safe detachment.

Each helper binding uses a private mapping context. This keeps rollback and
unbind ownership local to one handle and avoids editing an already-active
shared context. Helper-created actions default to non-consuming input. The
helper layer adds no discovery, hook, scan, polling loop, or game-state policy.
