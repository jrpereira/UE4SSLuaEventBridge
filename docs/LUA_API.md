# Lua API contract

## Namespace

Every Lua mod receives its own `UE4SSLuaEventBridge` table after that Lua state
starts.

```lua
local bridge = UE4SSLuaEventBridge
```

## Version and capabilities

```lua
local version = bridge.GetVersion()
local capabilities = bridge.GetCapabilities()
```

Version 0.2.11 reports API version 2:

```lua
{
    api = 2,
    enhanced_input = true,
    explicit_target = true,
    target_ue4ss_commit = "97b7e501",
}
```

Capabilities are authoritative. Consumers should not infer support from the
bridge version.

## Open an explicit input component

The caller supplies the exact live `UEnhancedInputComponent` object path. The
bridge does not discover a player controller, pawn, or component on the
caller's behalf.

```lua
local target, err = bridge.OpenInputComponent(componentPath)
if target == nil then
    error(err)
end
```

Object lookup occurs only during `OpenInputComponent`. The returned handle is
owned by the calling Lua session.

## Bind an action

The caller also supplies the exact `UInputAction` object path:

```lua
local handle, err = bridge.BindAction(
    target,
    actionPath,
    "Triggered",
    function(event)
        print(event.action, event.phase, event.value.x)
    end)
```

Supported event names are:

- `Started`
- `Ongoing`
- `Triggered`
- `Canceled`
- `Completed`

The callback receives:

```lua
{
    subscription = 1,
    source_type = "enhanced_input",
    action = "/Game/Input/IA_Example.IA_Example",
    phase = "Triggered",
    elapsed_processed = 0.201,
    elapsed_triggered = 0.0,
    value = { x = 1.0, y = 0.0, z = 0.0, type = 0 },
}
```

`value.type` uses Unreal's Enhanced Input value-type values: Boolean `0`,
Axis1D `1`, Axis2D `2`, and Axis3D `3`.

## Thread requirement

`OpenInputComponent`, `BindAction`, `Unbind`, `CloseInputComponent`, and
`UnbindAll` must run on the Unreal game thread because they resolve or mutate
live Unreal objects. Calls from other threads fail without touching the native
binding array. UE4SS `ExecuteInGameThread` child Lua states are supported and
retain the owning Lua session ID.

Callbacks are not invoked inside Unreal's input-dispatch stack. Native events
are queued and delivered from the bridge's UE4SS update callback.

## Ownership and cleanup

```lua
bridge.Unbind(handle)
bridge.CloseInputComponent(target)
bridge.UnbindAll()
```

A target or subscription handle can only be used by the Lua session that
created it. Closing a target first unbinds every subscription attached through
that target. Because the caller owns lifecycle policy, it should call
`UnbindAll` on the game thread before stopping or reloading its Lua mod.

Lua-stop still deactivates every callback immediately. If Lua-stop runs off the
game thread, native array mutation is deferred; the next game-thread bridge
operation reaps those inactive bindings. This avoids racing Unreal's input
dispatcher merely to perform hidden cleanup.

Callback closures are owned by a Lua-side table, not by one permanent registry
reference per subscription. They are released on bind failure, explicit
unbind, target close, unbind-all, or callback failure. One dispatcher registry
reference remains for the lifetime of the Lua state and is reclaimed when that
state closes.

If UE4SS unloads the C++ bridge while Unreal still owns a native binding or an
engine-created clone, the bridge DLL pins itself for the rest of the process.
That fail-safe prevents Unreal from retaining a vtable into unloaded code.
Correct caller-owned unbinding avoids the pin.

The caller owns rebinding policy. If the game reconstructs its input component,
close the old target, open the new exact component path, and bind the actions
again.
