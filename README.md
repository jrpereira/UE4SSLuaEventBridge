# UE4SS Lua Event Bridge

`UE4SSLuaEventBridge` is a native UE4SS C++ mod that exposes native Unreal
event sources to Lua mods through a small, capability-based API.

The bridge is deliberately game-agnostic. Consumer mods identify event sources
and provide callbacks; the bridge owns native bindings, Lua registry references,
thread hand-off, and cleanup.

## Current status

Version `0.1.0-dev` defines and tests the public contract and lifecycle model.
The Enhanced Input adapter is specified but not yet compiled because the
generated CXX header dump contains reflected layouts only. A native
`UEnhancedInputComponent::BindAction` adapter additionally needs the matching
UE4SS source/SDK and a verified UE 5.5 native call path.

This package is source code, not an installable release. It intentionally does
not contain a placeholder `main.dll`.

## Design rules

- No game-specific actions, object paths, keys, or UI behavior.
- One isolated session per Lua mod/state.
- Opaque subscription handles rather than exposed native pointers.
- Callbacks execute on the Unreal game thread.
- Lua callbacks are never retained after their Lua mod stops.
- Native bindings are detached before their target object becomes invalid.
- Backends advertise capabilities; unsupported event types fail explicitly.
- Consumers can rebind declaratively after world or input-component changes.

## Proposed layout after compilation

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

