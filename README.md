# UE4SS Lua Event Bridge

`UE4SSLuaEventBridge` is a native UE4SS C++ mod that exposes native Unreal
event sources to Lua mods through a small, capability-based API.

The bridge is deliberately game-agnostic. Consumer mods identify event sources
and provide callbacks; the bridge owns native bindings, Lua registry references,
thread hand-off, and cleanup.

## Current status

Version `0.2.2` contains an ABI-pinned Enhanced Input backend and the Lua
`BindAction` API. It inserts native action-event bindings into the active local
player's `UEnhancedInputComponent`; Unreal remains responsible for evaluating
input mappings and triggers. Native events are queued and delivered to the
owning Lua state from UE4SS's normal update thread.

Version 0.2.2 registers UE4SS's parent, main, async, and hook Lua threads as
aliases of one owning session. A callback API call made from a mod's main Lua
thread therefore resolves the same session created by `on_lua_start`.

The backend has passed portable unit tests, a cross-platform C++ syntax audit,
and export-name verification against the supplied UE4SS DLL. Release archives
are built with MSVC by GitHub Actions against the ABI-pinned import definition.

## Design rules

- No game-specific actions, object paths, keys, or UI behavior.
- One isolated session per Lua mod/state.
- Opaque subscription handles rather than exposed native pointers.
- Native bindings are created and removed on the Unreal game thread.
- Lua callbacks execute on UE4SS's Lua-owning update thread, never from inside
  Unreal's input-dispatch stack.
- Lua callbacks are never retained after their Lua mod stops.
- Native bindings are detached before their target object becomes invalid.
- Backends advertise capabilities; unsupported event types fail explicitly.
- The bridge automatically reattaches subscriptions after pawn or input-
  component reconstruction.

## Layout after compilation

```text
Mods/
└── UE4SSLuaEventBridge/
    ├── dlls/
    │   └── main.dll
    └── enabled.txt
```

## Documentation

- [`docs/LUA_API.md`](docs/LUA_API.md) — public Lua contract
- [`docs/ENHANCED_INPUT_BACKEND.md`](docs/ENHANCED_INPUT_BACKEND.md) — native
  adapter requirements and safety boundary
- [`docs/BUILD.md`](docs/BUILD.md) — ABI-pinned build requirements

## Compatibility target

- Windows x64
- UE4SS `3.0.1 Beta #0`, commit `97b7e501`
- Unreal Engine `5.5`

The C++ ABI must match the installed UE4SS build. A DLL built against a nearby
experimental commit is not assumed compatible.
