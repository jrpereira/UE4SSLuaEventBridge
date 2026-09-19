# Lifecycle regression tests

Run from the repository root with Lua 5.4:

```sh
bash tests/run-lua-tests.sh
```

Without Bash, run both files directly:

```sh
lua tests/LuaHelperTests.lua
lua tests/LifecycleIntegrationTests.lua
```

The build and release workflows invoke the same runner. A failed assertion
returns a nonzero exit code and fails the job.

## What is exercised

The lifecycle suite loads the production `mod/lua/bridge_api.lua` into isolated
Lua environments. Each reload receives a fresh session ID and callback registry,
while its simulated engine retains shared context, target and subscription
registries. Assertions cover cleanup ownership, error propagation, callback
routing and absence of accumulated resources.

| Scenario | Regression checks |
| --- | --- |
| Off-thread helper shutdown | Rejects cleanup without reflected mutations; retains resources for a later game-thread retry. |
| Repeated reloads | 100 fresh environments; all four debug observers removed; cleanup is idempotent; old queued callbacks never invoke user code. |
| Mapping-context removal failures | Two consecutive failures are reported; retry ownership is retained; detached callbacks stay disabled; successful removal happens once. |
| Partial native removal failure | Remaining subscription survives for retry; already-removed context is not removed twice. |
| Component destruction | Helper teardown completes under the native dead-component unsubscribe contract; captured callbacks are ignored after teardown. |
| Late callbacks | An event captured before closing a scope cannot invoke that scope or a subsequently opened scope. |
| Subsystem destruction | Invalid subsystem is not called through during cleanup; subscriptions and targets are released. |
| Native-stop helper failure | Failed cleanup throws and attempts a Lua log message; the native stop path reports the exception and still detaches native actions. |
| Thrown scope cleanup errors | An unexpected exception from one scope does not abandon healthy scopes; the failed scope remains retryable while Lua is alive. |
| Public UnbindAll retries | Context failure, partial subscription failure, and bulk failure preserve retry ownership; captured callbacks stay disabled; later Close/UnbindAll releases all resources. |
| Multiple scopes | Failure in one scope does not prevent cleanup of other scopes; remaining work is retried. |

## Native queue ownership coverage

`QueueBuffersTests` additionally runs 100 lifecycle cycles with real producer,
consumer and statistics-reader threads synchronized by barriers, without sleeps.
It uses the production buffer transfer, dispatch backlog, count publisher and
budgeted dispatcher. It checks producer refill during a retained batch, count
visibility before callbacks, canceled-entry draining, shared ownership release,
and clearing the producer queue at shutdown. These are native C++ queue tests;
the payload is not a UObject and this does not establish engine GC safety.

The Windows build and release jobs run the queue and backend suites with MSVC AddressSanitizer
and assertions enabled. Locally, run `tests/NativeQueueSanitizerTests.ps1` from an
x64 Visual Studio developer shell. Sanitizer failures fail the job. AddressSanitizer
checks memory access errors; it is not a data-race detector or an Unreal GC test.

## Native backend lifecycle coverage

On Windows, `NativeBackendLifecycleTests` compiles the production
`EnhancedInputBackend.cpp` with controlled UE imports and object-array fixtures.
It exercises actual native delegate allocation, attachment, execution, cloning,
queueing, deactivation, detachment and backend shutdown:

- 100 sessions bind, enqueue, unsubscribe and release retained native ownership.
- Engine-owned clones reject late delivery after original removal and remain
  safe to call and destroy after the backend has been destroyed.
- A producer executes 10,000 delegate calls while a second thread shuts down
  the backend; off-thread shutdown leaves engine-owned bindings untouched,
  rejects subsequent events and requires module retention until destruction.
- Destroying the component and its delegates before unsubscribe exercises the
  real dead-component branch without dereferencing freed native bindings.
- Failed native allocation publishes no binding and allows a subsequent retry.

The local native candidate, Windows developer build and Windows CTest include
this suite. Build/release CI runs it alongside the queue test under MSVC
AddressSanitizer. This tests production native code, but UE object discovery,
allocation, thread identity and object-array lifetime are controlled stubs.
It does not run real Unreal GC, Lua teardown or the engine's delegate dispatcher.

## Target delivery-fault coverage

The native backend suite additionally verifies real bounded-queue rejection of a
terminal event, invalidation of a retained old batch, notification without further
input, rejection of new subscriptions on the faulted target, explicit new-target
recovery, unaffected legacy/other-target behavior, and an injected C++ vector
allocation exception. Closing a target or stopping its session suppresses its
pending notification. The Lua suite exercises one-shot notification, a throwing
fault handler, stale callback suppression, and a scheduled side effect rejected
by native-health/generation guards before and after notification. These tests do
not establish game-thread integration or physical focus/key-state semantics.

## Validation boundary

The Lua suites are **offline helper integration tests**, using simulated Unreal objects
and native exports. They execute the real Lua implementation, but do not execute
`BridgeMod::on_lua_stop`, native delegate detachment, engine garbage collection,
or concurrent engine threads. Component destruction models the native contract;
it does not establish that the pinned Unreal ABI implements it safely.

The off-thread case invokes the helper cleanup entry point with the thread guard
set to false. Native off-thread Lua shutdown currently skips that entry point and
deactivates subscriptions without removing generated contexts. Game-thread stop now
reports helper cleanup failures instead of discarding their return values, but
reporting does not preserve those contexts for cleanup after the Lua state dies. Passing this suite
does **not** resolve or certify that known lifecycle gap.

Native acceptance still requires an isolated compatible UE/UE4SS process:
stop Lua off-thread, reload it repeatedly, destroy/recreate the input component,
and deliver queued or in-flight events during teardown. Verify mapping-context
and delegate counts, absence of callbacks into stopped sessions, and absence of
crashes using evidence from that process. Do not treat simulated cleanup or
portable weak-pointer tests as proof of those runtime properties.
