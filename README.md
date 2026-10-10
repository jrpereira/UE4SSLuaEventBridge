# UE4SS Lua Extension Bridges

Native C++ bridges that extend UE4SS Lua mods. Each product lives in its own
folder, is versioned and released separately, and installs as its own mod.

| Product | Folder | What it does |
| --- | --- | --- |
| [UE4SS Lua Event Bridge](bridge-events/README.md) | `bridge-events/` | Unreal Enhanced Input events for Lua callbacks, with native Tap and Hold triggers. |

A file bridge (`UE4SSLuaFileBridge`, file and folder access for Lua) is in
development and will be added here as `bridge-files/`.

## Repository layout

- `bootstrap/`: the `main.dll` loader that selects and starts a product's versioned DLL.
- `contract/ImplementationABI.h`: the private ABI shared by the bootstrap and the products.
- `bridge-events/`: the event bridge's sources, Lua API, tests, docs, examples and packaging.
- `tools/`, `tests/`: repository-level build, release and hygiene tooling and the test runners.

Build and test instructions are in the
[event bridge maintainer guide](bridge-events/docs/BUILD.md).
