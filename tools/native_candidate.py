"""Build and identify a local bridge DLL from a fresh, tested CMake build."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from deployment_preflight import digest


def sources(root):
    paths = [root/'CMakeLists.txt']
    for directory in ['mod', 'abi', 'cmake', 'tests']:
        paths.extend(p for p in (root/directory).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix not in {'.pyc','.pyo'})
    result = {}
    for path in paths:
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError('Native source must reside within the project')
        result[path.relative_to(root).as_posix()] = digest(path)
    if any(value is None for value in result.values()):
        raise ValueError('Missing native build source')
    return dict(sorted(result.items()))


def version(root):
    text = (root/'mod/include/UE4SSLuaEventBridge/Version.hpp').read_text()
    match = re.search(r'^#define UE4SSLEB_VERSION "([^"]+)"', text, re.M)
    if not match:
        raise ValueError('Bridge version missing')
    return match[1]


def build(root, output, runner=subprocess.run):
    root, output = root.resolve(), output.resolve()
    if output.exists():
        raise ValueError('Use a new output directory; never certify an existing DLL')
    before = sources(root)
    value = version(root)
    output.mkdir(parents=True)
    commands = [
        ['cmake','-S',str(root),'-B',str(output),'-G','Ninja',
         '-DCMAKE_BUILD_TYPE=Release','-DUE4SSLEB_BUILD_TESTS=ON',
         '-DUE4SSLEB_COMPONENT_LAYOUT_128=OFF'],
        ['cmake','--build',str(output),'--config','Release'],
        ['ctest','--test-dir',str(output),'--output-on-failure'],
    ]
    for command in commands:
        runner(command, check=True)
    if sources(root) != before:
        raise RuntimeError('Source changed during build; no candidate manifest produced')
    staged = output/'dist/UE4SSLuaEventBridge'
    files = {p:digest(staged/p) for p in ['dlls/main.dll','enabled.txt']}
    if any(value is None for value in files.values()):
        raise RuntimeError('Native build did not produce the complete Windows DLL payload')
    manifest = {'module':'UE4SSLuaEventBridge','version':value,'files':files,
                'native_provenance':{'source_files':before,'configuration':'Release',
                                     'experimental_component_layout':False,'tests_passed':True}}
    path = output/'candidate-manifest.json'
    path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return path


def validate(manifest, root):
    provenance = manifest.get('native_provenance', {})
    if manifest.get('module') != 'UE4SSLuaEventBridge' or set(manifest.get('files',{})) != {'dlls/main.dll','enabled.txt'}:
        raise ValueError('Unexpected native candidate payload')
    if provenance.get('configuration') != 'Release' or provenance.get('experimental_component_layout') is not False or provenance.get('tests_passed') is not True:
        raise ValueError('Candidate lacks a verified production build record; rebuild with native_candidate.py')
    captured, current = provenance.get('source_files', {}), sources(root)
    return sorted(path for path in set(captured)|set(current) if captured.get(path) != current.get(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--working', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(build(args.working, args.output))


if __name__ == '__main__':
    main()
