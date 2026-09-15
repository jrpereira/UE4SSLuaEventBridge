# Build requirements

## Required inputs

- Visual Studio 2022 with the Desktop C++ workload
- CMake 3.22 or newer
- Ninja or the Visual Studio 2022 CMake generator

The build is self-contained. Its minimal declarations and import definition are
pinned to the supplied `UE4SS.dll` at commit `97b7e501`; it does not build or
link a second UE4SS checkout. The Dawnwalker CXX dump supplies the verified UE
5.5 object and `FInputActionInstance` layouts used by the backend.

## Configure and build

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUE4SSLEB_BUILD_TESTS=OFF
cmake --build build
```

Expected output:

```text
build\dist\UE4SSLuaEventBridge\dlls\main.dll
```

Do not distribute a binary built from a different UE4SS experimental commit
without explicitly validating that its C++ ABI matches `97b7e501`.

## Portable checks

On Linux, the behavioral tests and ABI-facing syntax audit can be run without
the Windows SDK:

```sh
bash tests/run-portable-tests.sh
bash tests/run-abi-syntax-test.sh
```
