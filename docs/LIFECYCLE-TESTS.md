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
| Multiple scopes | Failure in one scope does not prevent cleanup of other scopes; remaining work is retried. |

## Validation boundary

These are **offline helper integration tests**, using simulated Unreal objects
and native exports. They execute the real Lua implementation, but do not execute
`BridgeMod::on_lua_stop`, native delegate detachment, engine garbage collection,
or concurrent engine threads. Component destruction models the native contract;
it does not establish that the pinned Unreal ABI implements it safely.

The off-thread case invokes the helper cleanup entry point with the thread guard
set to false. Native off-thread Lua shutdown currently skips that entry point and
deactivates subscriptions without removing generated contexts. Passing this suite
does **not** resolve or certify that known lifecycle gap.

Native acceptance still requires an isolated compatible UE/UE4SS process:
stop Lua off-thread, reload it repeatedly, destroy/recreate the input component,
and deliver queued or in-flight events during teardown. Verify mapping-context
and delegate counts, absence of callbacks into stopped sessions, and absence of
crashes using evidence from that process. Do not treat simulated cleanup or
portable weak-pointer tests as proof of those runtime properties.
