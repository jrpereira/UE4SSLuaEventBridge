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

## Tagged releases

Pushing a `release/vVERSION` branch from the intended release commit
runs the full portable checks, verifies that the branch matches the version
declared in `mod/include/UE4SSLuaEventBridge/Version.hpp`, and builds the Windows DLL with MSVC. After those
steps pass, the workflow creates the matching `vVERSION` tag and a
permanent GitHub release containing the installable ZIP and its SHA-256
checksum.
Prerelease suffixes such as `-rc.1` are supported and produce GitHub prereleases.
Opening a PR runs validation through build.yml and never publishes its unmerged
head. An explicit workflow dispatch on the matching release branch supports
publishers whose ref APIs do not emit push events. Existing releases are not
overwritten; concurrent runs for one ref are serialized.

Add `.github/release-notes/vMAJOR.MINOR.PATCH.md` before tagging to supply a
curated changelog. If that file is absent, GitHub-generated notes are used.

```sh
git switch main
git pull --ff-only
git switch -c release/v1.0.0
git push origin release/v1.0.0
```

## Portable checks

On Linux, the behavioral tests and ABI-facing syntax audit can be run without
the Windows SDK:

```sh
bash tests/run-portable-tests.sh
bash tests/run-abi-syntax-test.sh
```
