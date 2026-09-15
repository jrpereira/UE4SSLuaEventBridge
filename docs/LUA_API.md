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

Capabilities are authoritative. Consumers must not infer support from the
bridge version.

Planned capability keys:

```lua
{
    api = 1,
    enhanced_input = true,
    unreal_delegates = false,
    target_ue4ss_commit = "97b7e501"
}
```

## Enhanced Input subscription

The first complete backend will expose:

```lua
local handle, err = bridge.SubscribeEnhancedInput({
    action = "/Game/Input/IA_Example.IA_Example",
    event = "Triggered",
    receiver = "local_player",
}, function(event)
    print(event.action, event.phase, event.elapsed_processed)
end)
```

Supported phases follow Unreal's `ETriggerEvent` exactly:

- `Started`
- `Ongoing`
- `Triggered`
- `Canceled`
- `Completed`

The callback payload will be:

```lua
{
    subscription = 1,
    source_type = "enhanced_input",
    action = "/Game/Input/IA_Example.IA_Example",
    phase = "Triggered",
    elapsed_processed = 0.201,
    elapsed_triggered = 0.0,
    value = { x = 1.0, y = 0.0, z = 0.0 },
}
```

## Ownership and cleanup

Subscription handles belong to the Lua state that created them. Another Lua
mod cannot unsubscribe them.

```lua
bridge.Unsubscribe(handle)
bridge.UnsubscribeAll()
```

Stopping or reloading a Lua mod automatically performs `UnsubscribeAll()` for
that state. Native callbacks must not call a stopped Lua state.

## Rebinding

The bridge stores subscription intent separately from the live native binding.
When a player controller or Enhanced Input component is reconstructed, the
backend detaches the obsolete binding and attaches the same subscription to the
new component. The Lua callback and handle remain unchanged.

