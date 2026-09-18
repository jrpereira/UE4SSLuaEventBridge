"""Package the separate tests with repository-relative paths; no sample/runtime archives."""
from pathlib import Path
import hashlib
import json
import zipfile

SOURCE_FILES = '''
docs/QUEUE_DISPATCH_RATE.md
mod/include/UE4SSLuaEventBridge/QueueBuffers.hpp
mod/include/UE4SSLuaEventBridge/QueueDispatchSchedule.hpp
benchmarks/CMakeLists.txt
benchmarks/FlowBenchmark.cpp
benchmarks/run-flow-benchmark.ps1
benchmarks/sample_callback.lua
benchmarks/runner/README.md
benchmarks/runner/ClockMod.cpp
benchmarks/runner/IsolatedViewer.cpp
benchmarks/runner/adapter.lua
benchmarks/runner/batch-config.example.json
benchmarks/runner/batch-configuration.ps1
benchmarks/runner/benchmark.lua
benchmarks/runner/build-clock.ps1
benchmarks/runner/build-viewer.ps1
benchmarks/runner/expand-checked.ps1
benchmarks/runner/frogfu-adapter.lua
benchmarks/runner/frogfu-low-render-adapter.lua
benchmarks/runner/frogfu-no-world-render-adapter.lua
benchmarks/runner/guest.ps1
benchmarks/runner/package-tests.py
benchmarks/runner/run-comparison-batch.ps1
benchmarks/runner/run-sandbox-test.ps1
benchmarks/runner/runner-status.ps1
benchmarks/runner/send-sweep.ps1
benchmarks/runner/summarize-comparison-batch.py
benchmarks/runner/test-batch-configuration.ps1
benchmarks/runner/test-consumer.lua
benchmarks/runner/test-cycle-summary.py
benchmarks/runner/test-package.py
benchmarks/runner/test-runner-status.ps1
benchmarks/runner/test-staging.ps1
benchmarks/unreal/README.md
benchmarks/unreal/BridgeBench.uproject
benchmarks/unreal/Config/DefaultEngine.ini
benchmarks/unreal/Config/DefaultGame.ini
benchmarks/unreal/Config/DefaultInput.ini
benchmarks/unreal/Mods/BridgeFlowBenchmark/enabled.txt
benchmarks/unreal/Mods/BridgeFlowBenchmark/Scripts/main.lua
benchmarks/unreal/Source/BridgeBench.Target.cs
benchmarks/unreal/Source/BridgeBenchEditor.Target.cs
benchmarks/unreal/Source/BridgeBench/BridgeBench.Build.cs
benchmarks/unreal/Source/BridgeBench/BridgeBench.cpp
benchmarks/unreal/Source/BridgeBench/BridgeBench.h
benchmarks/unreal/send-keypress-sweep.ps1
benchmarks/unreal/test-mod.lua
'''.split()
BINARY_FILES = (
    'build/sandbox-clock/main.dll',
    'build/isolated-viewer/IsolatedViewer.exe',
    'mod/include/UE4SSLuaEventBridge/UE4SSABI.hpp',
    'build/release/UE4SS-97b7e501.lib',
    'build/sandbox-layout128/dist/UE4SSLuaEventBridge/dlls/main.dll',
)


def package(repo, output):
    files = [repo / relative for relative in (*SOURCE_FILES, *BINARY_FILES)]
    for path in files:
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(repo.resolve()):
            raise ValueError(f'Missing or unsafe test artifact: {path.relative_to(repo)}')
    checksum = output.with_suffix(output.suffix + '.sha256')
    if output.exists() or checksum.exists():
        raise FileExistsError('Refusing to overwrite an existing test bundle or checksum')
    manifest = {
        'schema': 1,
        'scope': 'Separate test harness. Experimental layout128 bridge is not the production ABI.',
        'excluded': ['sample application', 'UE4SS runtime', 'Microsoft CRT', 'third-party UEBridge',
                     'internal reports', 'raw session records', 'personal configuration'],
        'files': {p.relative_to(repo).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(repo).as_posix())
        archive.writestr('test-bundle-manifest.json', json.dumps(manifest, indent=2))
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC validation failed')
        for name, expected in manifest['files'].items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise ValueError(f'Packaged hash mismatch: {name}')
    with checksum.open('x') as stream:
        stream.write(hashlib.sha256(output.read_bytes()).hexdigest() + '\n')
    return manifest


if __name__ == '__main__':
    import argparse
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=repo / 'build/candidates/UE4SSLuaEventBridge-separate-tests.zip')
    args = parser.parse_args()
    manifest = package(repo, args.output)
    print(f'Verified {len(manifest["files"])} allowlisted files: {args.output}')
