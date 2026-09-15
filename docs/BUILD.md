# Build requirements

## Required inputs

- Visual Studio 2022 with the Desktop C++ workload
- CMake 3.22 or newer
- Git with submodules
- Access to the UE4SS build dependencies required by the official template
- `RE-UE4SS` checked out at commit `97b7e501`
- The native Enhanced Input backend once its Dawnwalker call boundary is
  verified

The supplied `UE4SS.dll` proves the installed ABI target. The supplied
`CXXHeaderDump` provides reflected game layouts. Neither file contains the
UE4SS development headers or a callable address for the native-only
`UEnhancedInputComponent::BindAction` method.

## Configure and build

```bat
git submodule update --init --recursive
git -C RE-UE4SS checkout 97b7e501
git -C RE-UE4SS submodule update --init --recursive

cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Game__Shipping__Win64
```

Expected output after the native backend is enabled:

```text
build\dist\UE4SSLuaEventBridge\dlls\main.dll
```

Do not distribute a binary built from a different UE4SS experimental commit
without explicitly validating that its C++ ABI matches `97b7e501`.

