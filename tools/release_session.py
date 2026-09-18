"""One-shot deployment/launch gate; no gameplay polling or persisted ignore flag."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from deployment_preflight import compare, digest, safe_file


def game_session():
    if os.name != 'nt':
        raise RuntimeError('Live process detection requires Windows')
    command = "@(Get-Process -Name Dawnwalker -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{pid=$_.Id;started_at=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress"
    raw = subprocess.check_output(['powershell', '-NoProfile', '-NonInteractive', '-Command', command], text=True, timeout=20).strip()
    value = json.loads(raw) if raw else None
    if isinstance(value, list):
        if len(value) > 1:
            raise RuntimeError('Multiple Dawnwalker processes; resolve before deployment or launch')
        value = value[0] if value else None
    return value


def deploy(manifest, staged, deployed, records, session_provider=game_session):
    """Copy only verified manifest entries; preserve settings and enablement."""
    if session_provider():
        raise RuntimeError('Close the game before deployment, then repeat this command')
    entries = manifest['files']
    if not entries:
        raise ValueError('Empty payload')
    if len({p.casefold() for p in entries}) != len(entries):
        raise ValueError('Case-insensitive payload path collision')
    for relative, expected in entries.items():
        name = Path(relative).name.lower()
        if name == 'config.ini' or (name.endswith(('.ini', '.json', '.toml', '.cfg')) and name not in {'mod_settings.ini', 'config.example.ini'}):
            raise ValueError('Personal settings cannot be deployment payload: '+relative)
        if digest(safe_file(staged, relative)) != expected:
            raise ValueError('Staged hash mismatch: '+relative)
        safe_file(deployed, relative)
    records = records.resolve()
    if records.is_relative_to(deployed.resolve()) or records.is_relative_to(staged.resolve()):
        raise ValueError('Keep deployment records outside payload roots')
    backup = records / datetime.now(timezone.utc).strftime('deploy-%Y%m%dT%H%M%S%fZ')
    backup.mkdir(parents=True, exist_ok=False)
    original = {}
    if deployed.exists():
        for path in deployed.rglob('*'):
            if path.is_symlink():
                raise ValueError('Review deployed symlink before copying: '+str(path))
            if path.is_file():
                relative = path.relative_to(deployed).as_posix()
                original[relative] = digest(path)
                target = safe_file(backup/'before', relative)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
                if digest(target) != original[relative]:
                    raise RuntimeError('Backup verification failed: '+relative)
    state = {'module': manifest['module'], 'version': manifest['version'], 'before': original,
             'payload': entries, 'changed': [], 'status': 'backed-up'}
    def record():
        (backup/'result.json').write_text(json.dumps(state, indent=2)+'\n', encoding='utf-8')
    record()
    try:
        for relative, expected in entries.items():
            if session_provider():
                raise RuntimeError('Game started during deployment; stop and review the recorded partial deployment')
            target = safe_file(deployed, relative)
            if digest(target) != original.get(relative):
                raise RuntimeError('Concurrent destination change: '+relative)
            if relative == 'enabled.txt' and target.exists():
                continue
            if digest(target) != expected:
                source = safe_file(staged, relative)
                if digest(source) != expected:
                    raise RuntimeError('Staged file changed during deployment: '+relative)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                state['changed'].append(relative)
            if digest(target) != expected:
                raise RuntimeError('Installed hash mismatch: '+relative)
        for relative, expected in original.items():
            if relative not in entries or relative == 'enabled.txt':
                if digest(safe_file(deployed, relative)) != expected:
                    raise RuntimeError('Preserved file changed: '+relative)
        state['status'] = 'deployed'
        record()
    except Exception as error:
        state['status'] = 'incomplete'
        state['error'] = str(error)
        record()
        raise
    return backup


def launch(result, command, ignore_once=False, session_provider=game_session, runner=subprocess.Popen):
    if session_provider():
        raise RuntimeError('Game is already running; inspect that session instead')
    if result['needs_attention'] and not ignore_once:
        raise RuntimeError('Version check needs attention: correct the mismatch or choose --ignore-once for this launch')
    if not command:
        raise ValueError('Supply the game executable and arguments after --')
    return runner(command, shell=False)


def inspect_candidate(manifest, staged, deployed, working, session=None, evidence=None):
    if 'native_provenance' in manifest:
        from native_candidate import validate, version
        changes = validate(manifest, working)
        result = compare(manifest, staged, deployed, session, evidence, working_version=version(working))
        result['working_differences'] = changes
        if changes:
            result['needs_attention'] = True
            result['options'] = ['Rebuild current native source with native_candidate.py, then repeat verification.',
                                 'Ignore this discrepancy for this launch only; retain the warning in the test record.']
        return result
    from package_manifest import definition, version
    spec = definition(working)
    if manifest['module'] != spec['module'] or set(manifest['files']) != set(spec['files']):
        raise ValueError('Candidate must match the current module and complete release definition')
    return compare(manifest, staged, deployed, session, evidence, working, spec, version(working))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'deploy', 'launch'])
    for name in ['manifest', 'staged', 'deployed', 'working', 'records']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--runtime-evidence', type=Path)
    parser.add_argument('--log', type=Path, help='Current UE4SS.log; correlates Lua version messages to process boot')
    parser.add_argument('--bridge-dir', type=Path, help='Optional diagnostic UEBridge protocol-3 folder for native version query')
    parser.add_argument('--ignore-once', action='store_true')
    argv = sys.argv[1:]
    split = argv.index('--') if '--' in argv else len(argv)
    args = parser.parse_args(argv[:split])
    command = argv[split+1:]
    if command and args.action != 'launch':
        parser.error('Unexpected arguments')
    if args.ignore_once and args.action != 'launch':
        parser.error('--ignore-once applies only to one launch')
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    evidence = json.loads(args.runtime_evidence.read_text(encoding='utf-8-sig')) if args.runtime_evidence else None
    current_session = game_session()
    if args.bridge_dir and manifest['module'] == 'UE4SSLuaEventBridge' and not evidence:
        from runtime_evidence import from_bridge
        evidence = from_bridge(args.bridge_dir, current_session, game_session)
    if args.log and not evidence:
        from runtime_evidence import from_log
        evidence = from_log(args.log, manifest['module'], current_session)
        if game_session() != current_session:
            raise RuntimeError('Game session changed while reading runtime evidence; repeat the check')
    result = inspect_candidate(manifest, args.staged, args.deployed, args.working, current_session, evidence)
    print(json.dumps(result, indent=2))
    args.records.mkdir(parents=True, exist_ok=True)
    record = args.records / datetime.now(timezone.utc).strftime('preflight-%Y%m%dT%H%M%S%fZ.json')
    record.write_text(json.dumps({'action': args.action, 'ignore_once': args.ignore_once, 'result': result}, indent=2)+'\n')
    if args.action == 'deploy':
        if result['stage_integrity_errors'] or result['working_differences'] or result['staged_version_differs_from_working']:
            raise RuntimeError('Rebuild/restage current working source before deployment')
        print(deploy(manifest, args.staged, args.deployed, args.records))
    elif args.action == 'launch':
        launch(result, command, args.ignore_once)
    else:
        return 2 if result['needs_attention'] else 0
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
