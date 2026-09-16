# Developer API: primitives and helpers

UE4SSLuaEventBridge 0.3.0 exposes two API layers:

| Layer | Use it when | Ownership |
|---|---|---|
| Library primitives | The game or another mod already owns the Input Actions and mappings | The caller owns Unreal assets and mapping contexts |
| Input helpers | A mod wants to bind a physical key without shipping Input Action assets | The returned input scope owns transient actions, triggers, mappings, and native subscriptions |

Both layers are game-agnostic. The caller supplies exact live object paths; the
bridge does not discover a player controller, pawn, local player, input
component, or subsystem.

## Requirements and execution model

Version 0.3.0 targets:

- UE4SS 3.0.1 Beta #0 at commit `97b7e501`;
- Unreal Engine 5.5; and
- Windows x64.

Operations that resolve or mutate Unreal objects must run on the Unreal game
thread. Initialize and shut down inside `ExecuteInGameThread`:

```lua
ExecuteInGameThread(function()
    -- Open, bind, unbind, and close here.
end)
```

Native input execution never enters Lua. It copies event data into a queue,
and the bridge dispatches Lua callbacks later from its UE4SS update callback.
If a callback needs to change bindings, it must schedule that work with
`ExecuteInGameThread`.

The bridge installs no `ProcessEvent` hook. Helpers do invoke ordinary
reflected Enhanced Input functions through UE4SS to create and register their
private objects.

## Version and capabilities

```lua
local version = UE4SSLuaEventBridge.GetVersion()
local capabilities = UE4SSLuaEventBridge.GetCapabilities()
```

Version 0.3.0 reports:

```lua
{
    api = 3,
    enhanced_input = true,
    explicit_target = true,
    helpers = true,
    dynamic_input = true,
    trigger_tap = true,
    trigger_hold = true,
    target_ue4ss_commit = "97b7e501",
}
```

Capabilities are authoritative. Consumers should not infer feature support
from a version comparison.

## Library primitives

Every Lua mod receives an isolated `UE4SSLuaEventBridge` table. Target and
subscription handles belong to the Lua session that created them.

### `OpenInputComponent(componentPath)`

```lua
local target, err = UE4SSLuaEventBridge.OpenInputComponent(componentPath)
```

`componentPath` is the exact live object path of a
`UEnhancedInputComponent`, not a class or content asset path. The component is
resolved only during this call and retained as a native weak reference.

Returns a numeric target handle, or `nil, errorMessage`.

### `BindAction(target, actionPath, eventName, callback)`

```lua
local handle, err = UE4SSLuaEventBridge.BindAction(
    target,
    "/Game/Input/IA_Example.IA_Example",
    "Triggered",
    function(event)
        print(event.action, event.phase)
    end)
```

This binds an existing `UInputAction`. It does not create an action, trigger,
key mapping, or mapping context.

Supported event names are `Started`, `Ongoing`, `Triggered`, `Canceled`, and
`Completed`. The callback receives:

```lua
{
    subscription = 1,
    source_type = "enhanced_input",
    action = "/Game/Input/IA_Example.IA_Example",
    phase = "Triggered",
    elapsed_processed = 0.501,
    elapsed_triggered = 0.0,
    value = { x = 1.0, y = 0.0, z = 0.0, type = 0 },
}
```

`value.type` uses Unreal's Enhanced Input values: Boolean `0`, Axis1D `1`,
Axis2D `2`, and Axis3D `3`. Both elapsed fields come from Enhanced Input; the
bridge does not measure time itself.

### `Unbind(handle)`

```lua
local removed = UE4SSLuaEventBridge.Unbind(handle)
```

Deactivates the callback and removes its native action-event binding. It
returns `true` when the calling session owns the handle and removal completes.

### `CloseInputComponent(target)`

```lua
local closed = UE4SSLuaEventBridge.CloseInputComponent(target)
```

Unbinds subscriptions attached through the target and releases the target
handle. It never destroys the Unreal component.

### `UnbindAll()`

```lua
local count = UE4SSLuaEventBridge.UnbindAll()
```

Attempts to close all helper scopes in the current Lua session, then removes
any remaining native subscriptions. It returns the number of native
subscriptions removed. Prefer each helper scope's `Close()` method because it
can report reflected mapping-context cleanup errors.

The compatibility aliases `SubscribeEnhancedInput`, `Unsubscribe`, and
`UnsubscribeAll` remain available.

## Input helpers

Helpers create private transient Enhanced Input objects so the mod can bind an
Unreal key name directly:

```lua
local Helpers = UE4SSLuaEventBridge.Helpers
local Trigger = Helpers.Trigger
```

The supported trigger constants are `Trigger.Tap` and `Trigger.Hold`. They are
opaque, read-only values. Enhanced Input—not Lua—decides whether Tap or Hold
fires.

### `Helpers.OpenInput(options)`

```lua
local input, err = Helpers.OpenInput({
    component_path = COMPONENT_PATH,
    subsystem_path = ENHANCED_INPUT_SUBSYSTEM_PATH,
    mapping_priority = 0,
})
```

Required options:

- `component_path`: exact live path of the `UEnhancedInputComponent` that
  receives generated action bindings.
- `subsystem_path`: exact live path of the
  `UEnhancedInputLocalPlayerSubsystem` that receives private mapping contexts.

`mapping_priority` is an optional signed 32-bit context priority and defaults
to `0`. `OpenInput` performs no discovery. It resolves both supplied objects
and returns an input scope, or `nil, errorMessage`. It does not install a
mapping until `Bind` succeeds.

### `input:Bind(key, trigger, callback [, options])`

```lua
local handle, err = input:Bind("F10", Trigger.Tap, function(event)
    print(event.key .. " tapped\n")
end, {
    threshold_seconds = 0.2,
})
```

Each successful call creates and owns one transient Input Mapping Context,
Boolean Input Action, Tap or Hold trigger, key mapping, and native `Triggered`
subscription. A private context per binding makes removal independent and
avoids mutating a context that is already active.

Generated actions default to `bConsumeInput = false`, so Tap and Hold actions
on the same key can both be evaluated and the helper does not intentionally
consume the game's mapping.

Common options:

| Option | Type | Default | Effect |
|---|---|---:|---|
| `consume_input` | boolean | `false` | Sets the generated Input Action's consume-input flag |
| `trigger_when_paused` | boolean | Unreal default | Sets the generated Input Action's paused-trigger flag |

Tap options:

| Option | Type | Default | Effect |
|---|---|---:|---|
| `threshold_seconds` | non-negative number | Unreal default | Sets `TapReleaseTimeThreshold` |

Hold options:

| Option | Type | Default | Effect |
|---|---|---:|---|
| `threshold_seconds` | non-negative number | Unreal default | Sets `HoldTimeThreshold` |
| `one_shot` | boolean | Unreal default | Sets `bIsOneShot` |

```lua
local handle, err = input:Bind("F10", Trigger.Hold, function(event)
    print(string.format("held for %.0f ms\n", event.elapsed_processed * 1000))
end, {
    threshold_seconds = 0.5,
    one_shot = true,
})
```

With `one_shot = false`, Enhanced Input may emit `Triggered` repeatedly while
the key remains actuated. The helper does not suppress or synthesize events.

The helper callback contains the primitive event payload plus:

```lua
{
    key = "F10",
    trigger = Trigger.Hold,
}
```

The `action` field is the transient generated action path. It is an
implementation detail and is not stable across sessions; use `key` and
`trigger` instead.

### `input:Unbind(handle)`

```lua
local removed, err = input:Unbind(handle)
```

Removes the native subscription and its private mapping context, then releases
the action and trigger references. A handle from another scope is rejected.
Do not pass helper-owned handles to the primitive `Unbind`; doing so bypasses
the helper's mapping-context ownership.

### `input:Close()`

```lua
local closed, err = input:Close()
```

Closes all bindings owned by the scope, removes their private mapping
contexts, and closes the component target. `Close` is idempotent. If a cleanup
step fails, ownership is retained so a later game-thread call can retry it.
`Bind` after a successful close returns `nil, "input scope is closed"`.

Call `Close` explicitly before a mod reload or shutdown. If UE4SS invokes the
bridge's Lua-stop callback on the game thread, the bridge also attempts to
close remaining helper scopes. On an off-thread Lua stop, it can safely
deactivate native callbacks but cannot mutate Enhanced Input mapping contexts;
explicit game-thread cleanup is therefore the reliable lifecycle contract.

## Complete Tap/Hold example

```lua
local Helpers = UE4SSLuaEventBridge.Helpers
local Trigger = Helpers.Trigger
local input

ExecuteInGameThread(function()
    local err
    input, err = Helpers.OpenInput({
        component_path = COMPONENT_PATH,
        subsystem_path = ENHANCED_INPUT_SUBSYSTEM_PATH,
    })
    if input == nil then error(err) end

    local tapHandle
    tapHandle, err = input:Bind("F10", Trigger.Tap, function()
        print("F10 tapped\n")
    end, { threshold_seconds = 0.2 })
    if tapHandle == nil then error(err) end

    local holdHandle
    holdHandle, err = input:Bind("F10", Trigger.Hold, function(event)
        print(string.format("F10 held for %.0f ms\n",
            event.elapsed_processed * 1000))
    end, { threshold_seconds = 0.5, one_shot = true })
    if holdHandle == nil then error(err) end
end)

local function shutdown()
    if input == nil then return end
    local closed, err = input:Close()
    if not closed then error(err) end
    input = nil
end

ExecuteInGameThread(shutdown)
```

See `examples/EnhancedInputTapHold/Scripts/main.lua` for a complete sample mod
with path placeholders, logging, failure rollback, and an explicit shutdown
entry point.

## Failure and lifetime behavior

Argument-contract violations raise Lua errors. Runtime failures return
`nil, errorMessage` or `false, errorMessage`.

Helper creation and binding are transactional. A failure removes any context
already installed. If reflected removal itself fails, the scope retains an
orphan cleanup record so `Close()` can retry. Earlier successful bindings in
the scope remain active.

The following invariants apply:

- no Lua callback runs after its native subscription is deactivated;
- Unreal binding arrays are never mutated off the game thread;
- generated objects remain referenced while their contexts or bindings are
  owned by the scope;
- one scope never removes another scope's mapping context;
- callbacks receive copied values outside Unreal's input-dispatch stack; and
- the DLL is conservatively retained if Unreal may still own a native binding
  vtable during bridge unload.

The helpers do not add controller discovery, UObject listeners,
`ProcessEvent` hooks, scanning, polling, automatic rebinding, gameplay-state
filtering, or game-specific behavior.
