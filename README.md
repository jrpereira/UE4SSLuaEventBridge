# UE4SS Lua Event Bridge

`UE4SSLuaEventBridge` is a native UE4SS C++ mod that exposes native Unreal
Enhanced Input action events to Lua through a small, explicit-target API.

The current build targets UE4SS `3.0.1 Beta #0` at commit `97b7e501` and Unreal
Engine 5.5 on Windows x64. The C++ and Unreal layouts are ABI-pinned; a build
for a nearby UE4SS or engine revision is not assumed compatible.

The product version is defined in
[`Version.hpp`](mod/include/UE4SSLuaEventBridge/Version.hpp) and returned by
`GetVersion()`. API compatibility is reported separately by `API_VERSION`.

## Why?

UE4SS gives Lua access to reflected Unreal functions, but binding a Lua callback
directly to an Enhanced Input action needs a native connection. Enhanced Input's
`BindAction` APIs are C++ binding helpers, not reflected functions that Lua can
simply call. UE4SS's `RegisterHook` observes existing `UFunction` calls and does
not support delegate functions; it does not create an Enhanced Input subscription.
See the [UE4SS hook documentation](https://docs.ue4ss.com/dev/lua-api/global-functions/registerhook.html)
and [Unreal binding API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/UEnhancedInputComponent/BindAction).

A mod can hook a suitable game-provided action handler if one exists, or track
key states and timing itself. The first approach depends on the game's exposed
functions; the second means implementing behaviors such as Tap and Hold in Lua.

UE4SSLuaEventBridge supplies that native connection: Unreal evaluates the input
action and its triggers, and the bridge queues the resulting event for delivery
to your Lua callback.

### Advantages

- **Native input behaviors.** Let Enhanced Input recognize Tap, Hold, and other
  configured triggers instead of recreating timing and key-state logic in Lua.
- **Less polling code.** Lua receives callbacks for bound input events rather
  than repeatedly checking every key. The bridge still checks its dispatch queue
  at a configurable frequency.
- **Simpler mods.** Input recognition stays in the engine; Lua focuses on what
  the mod should do when an action occurs. This reduces custom state tracking
  and maintenance.
- **Less dependence on Lua polling timing.** The engine recognizes input
  transitions before delivering callbacks to Lua. Recognition therefore does not
  depend on a Lua poll catching both the pressed and released states.
- **Configurable responsiveness.** Mods can adjust dispatch frequency to balance
  callback latency against queue-checking work.
- **Control over dispatch work.** Configurable queue limits and per-pass budgets
  let you balance callback latency against processing work while using native
  Enhanced Input behaviors.

## Helper API

The helper layer creates private transient Input Actions, triggers, and mapping
contexts. The caller still supplies exact live component and subsystem paths.

```lua
local Helpers = UE4SSLuaEventBridge.Helpers
local Trigger = Helpers.Trigger

local input, openError = Helpers.OpenInput({
    component_path = componentPath,
    subsystem_path = enhancedInputSubsystemPath,
    debug = false, -- Set true for end-to-end Enhanced Input trace lines.
    debug_label = "MyMod",
})

local tapHandle, tapError = input:Bind("F10", Trigger.Tap, function()
    print("F10 tapped\n")
end)

local holdHandle, holdError = input:Bind("F10", Trigger.Hold, function(event)
    print(string.format("F10 held for %.0f ms\n",
        event.elapsed_processed * 1000))
end, { threshold_seconds = 0.5, one_shot = true })

input:Unbind(tapHandle)
input:Close()
```

Tap/Hold classification and elapsed time come from Unreal Enhanced Input; the
helper does not implement Lua timers or a key-state machine.

## Primitive API

```lua
local target, openError =
    UE4SSLuaEventBridge.OpenInputComponent(componentPath)

local handle, bindError = UE4SSLuaEventBridge.BindAction(
    target,
    actionPath,
    "Triggered",
    function(event)
        print(event.action, event.phase, event.value.x)
    end)

UE4SSLuaEventBridge.Unbind(handle)
UE4SSLuaEventBridge.CloseInputComponent(target)
UE4SSLuaEventBridge.UnbindAll()
```

Primitive failures include a descriptive error return. `OpenInputComponent`
and `BindAction` return `nil, error`; `Unbind` and `CloseInputComponent` return
`false, error`; `UnbindAll` returns `count, completed, error`.

The caller supplies exact live object paths and owns bind/unbind/rebind policy.
The bridge performs no controller discovery, UObject listening, `ProcessEvent`
hooking, background scans, key-state polling, or game-specific state filtering.
Event and trace queues use scheduled dispatch: 20 passes/sec by default, with a
configurable rate. See
[`docs/QUEUE_DISPATCH_RATE.md`](docs/QUEUE_DISPATCH_RATE.md).

## Safety model

- Native binding-array access is restricted to the Unreal game thread.
- Component and action lookup is exact-path only.
- Component layout and array invariants are checked before mutation.
- Weak object references use verified object index/serial semantics.
- Native input execution only queues copied event data; it does not enter Lua.
- Callback closures are released promptly on all public teardown paths and on
  callback failure.
- Off-thread Lua-stop deactivates callbacks without mutating Unreal arrays;
  inactive bindings are reaped by the next game-thread bridge operation.
- A process-lifetime DLL pin is used only if UE4SS unloads the bridge while
  Unreal still owns a binding or clone, preventing a dangling native vtable.
- Handles are isolated by Lua session, including calls made from UE4SS
  `ExecuteInGameThread` child Lua states.
- Session and child-state alias indexes are synchronized; stopped sessions
  remain inert until bridge destruction so in-flight raw references cannot
  outlive their storage.

## Installation layout

Download the versioned ZIP and checksum from
[GitHub Releases](https://github.com/jrpereira/UE4SSLuaEventBridge/releases).
Extract the ZIP into the UE4SS `Mods` directory to produce:

```text
Mods/
└── UE4SSLuaEventBridge/
    ├── dlls/
    │   └── main.dll
    └── enabled.txt
```

## Documentation

- [`docs/LUA_API.md`](docs/LUA_API.md) — public Lua contract
- [`docs/DEVELOPER_API.md`](docs/DEVELOPER_API.md) — low-level primitives and
  `OpenInput` helper contract
- [`docs/ENHANCED_INPUT_BACKEND.md`](docs/ENHANCED_INPUT_BACKEND.md) — ABI,
  ownership, and lifetime model
- [`docs/BUILD.md`](docs/BUILD.md) — ABI-pinned build requirements
- [`docs/LIFECYCLE-TESTS.md`](docs/LIFECYCLE-TESTS.md) — lifecycle regression coverage and runtime validation limits
- [`docs/BINDING_SNAPSHOT.md`](docs/BINDING_SNAPSHOT.md) — on-demand binding inspection
- [`examples/EnhancedInputTapHold`](examples/EnhancedInputTapHold) — complete
  game-agnostic F10 Tap/Hold sample mod
