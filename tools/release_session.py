"""One-shot deployment/activation/launch gate; no gameplay polling or persisted ignore flag."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from deployment_preflight import compare, digest, safe_file


MOD_ENTRY = re.compile(r'^(?P<prefix>\s*)(?P<name>[^;:\r\n][^:\r\n]*?)(?P<separator>\s*:\s*)(?P<enabled>[01])(?P<suffix>\s*(?:;.*)?)$')


def activated_mods_bytes(raw, mod_name, preserve_disabled=False):
    """Enable one mod first while preserving all foreign lines byte-for-byte."""
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', mod_name):
        raise ValueError('Mod name must contain only letters, digits, dot, underscore or hyphen')
    bom = raw.startswith(b'\xef\xbb\xbf')
    try:
        text = raw.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise ValueError('mods.txt must be UTF-8') from error
    newline = '\r\n' if '\r\n' in text else ('\n' if '\n' in text else os.linesep)
    lines = text.splitlines(keepends=True)
    matches = []
    parsed = []
    for index, line in enumerate(lines):
        body = line.rstrip('\r\n')
        match = MOD_ENTRY.fullmatch(body)
        parsed.append(match)
        if match and match.group('name').strip().casefold() == mod_name.casefold():
            matches.append(index)
    if len(matches) > 1:
        raise ValueError('mods.txt contains duplicate entries for '+mod_name)

    target = matches[0] if matches else None
    if (target is not None and preserve_disabled
            and parsed[target].group('enabled') == '0'):
        return raw
    first_entry = next((index for index, match in enumerate(parsed) if match), len(lines))
    if target is not None and target == first_entry and parsed[target].group('enabled') == '1':
        return raw

    if target is None:
        target_line = f'{mod_name} : 1{newline}'
    else:
        line = lines.pop(target)
        match = parsed[target]
        start, end = match.span('enabled')
        target_line = line[:start] + '1' + line[end:]
        if not target_line.endswith(('\r', '\n')):
            target_line += newline

    remaining_parsed = []
    for line in lines:
        remaining_parsed.append(MOD_ENTRY.fullmatch(line.rstrip('\r\n')))
    insertion = next((index for index, match in enumerate(remaining_parsed) if match), len(lines))
    lines.insert(insertion, target_line)
    updated = ''.join(lines).encode('utf-8')
    return (b'\xef\xbb\xbf' + updated) if bom else updated


def activate_mod(mods_txt, mod_name, records, session_provider=None, preserve_disabled=False):
    """Atomically activate a mod through mods.txt with a verified external backup."""
    if session_provider is None:
        session_provider = game_session
    if session_provider():
        raise RuntimeError('Close the game before changing mods.txt, then repeat this command')
    mods_txt = mods_txt.resolve()
    records = records.resolve()
    if records.is_relative_to(mods_txt.parent):
        raise ValueError('Keep activation records outside the Mods directory')
    original = mods_txt.read_bytes() if mods_txt.is_file() else b''
    updated = activated_mods_bytes(original, mod_name, preserve_disabled)
    if updated == original:
        disabled_entry = any(
            match and match.group('name').strip().casefold() == mod_name.casefold()
            and match.group('enabled') == '0'
            for match in (MOD_ENTRY.fullmatch(line) for line in original.decode('utf-8-sig').splitlines()))
        status = 'preserved-disabled' if preserve_disabled and disabled_entry else 'already-active'
        return {'module': mod_name, 'status': status, 'mods_txt': str(mods_txt)}

    backup = records / datetime.now(timezone.utc).strftime('activate-%Y%m%dT%H%M%S%fZ')
    backup.mkdir(parents=True, exist_ok=False)
    if mods_txt.is_file():
        shutil.copy2(mods_txt, backup/'mods.txt.before')
        if (backup/'mods.txt.before').read_bytes() != original:
            raise RuntimeError('Activation backup verification failed')
    state = {
        'module': mod_name,
        'mods_txt': str(mods_txt),
        'before_sha256': hashlib.sha256(original).hexdigest() if mods_txt.is_file() else None,
        'after_sha256': hashlib.sha256(updated).hexdigest(),
        'status': 'backed-up',
    }
    def record():
        (backup/'result.json').write_text(json.dumps(state, indent=2)+'\n', encoding='utf-8')
    record()
    temporary = None
    try:
        if session_provider():
            raise RuntimeError('Game started during activation; mods.txt was not changed')
        current = mods_txt.read_bytes() if mods_txt.is_file() else b''
        if current != original:
            raise RuntimeError('Concurrent mods.txt change; activation was not applied')
        mods_txt.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=mods_txt.parent, prefix=mods_txt.name+'.', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(updated)
            stream.flush()
            os.fsync(stream.fileno())
        if temporary.read_bytes() != updated:
            raise RuntimeError('Temporary mods.txt verification failed')
        if session_provider():
            raise RuntimeError('Game started during activation; mods.txt was not changed')
        os.replace(temporary, mods_txt)
        temporary = None
        if mods_txt.read_bytes() != updated:
            raise RuntimeError('Activated mods.txt verification failed')
        state['status'] = 'activated'
        record()
    except Exception as error:
        if temporary and temporary.exists():
            temporary.unlink()
        state['status'] = 'incomplete'
        state['error'] = str(error)
        record()
        raise
    return backup


def game_session():
    if os.name != 'nt':
        raise RuntimeError('Live process detection requires Windows')
    command = "@(Get-Process -Name Dawnwalker -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{pid=$_.Id;started_at=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress"
    # Windows PowerShell startup can exceed 20 seconds on a busy host even for
    # this small query. A timeout is a hard failure, so allow enough startup
    # headroom rather than turning a confirmed-closed game into an installer
    # failure. Every mutation still performs its own fresh process check.
    raw = subprocess.check_output(['powershell', '-NoProfile', '-NonInteractive', '-Command', command], text=True, timeout=60).strip()
    value = json.loads(raw) if raw else None
    if isinstance(value, list):
        if len(value) > 1:
            raise RuntimeError('Multiple Dawnwalker processes; resolve before deployment or launch')
        value = value[0] if value else None
    return value


def deploy(manifest, staged, deployed, records, session_provider=game_session):
    """Copy verified entries; preserve existing enablement and activate fresh installs."""
    if session_provider():
        raise RuntimeError('Close the game before deployment, then repeat this command')
    fresh_install = not deployed.exists() or not any(path.is_file() for path in deployed.rglob('*'))
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
    state['fresh_install'] = fresh_install
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
            # Enablement is user-owned, including an absent marker on fresh installs.
            if relative == 'enabled.txt':
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
        if fresh_install:
            activation = activate_mod(
                deployed.parent/'mods.txt', manifest['module'], backup/'activation',
                session_provider, preserve_disabled=True)
            state['activation'] = str(activation) if isinstance(activation, Path) else activation
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


def inspect_candidate(manifest, staged, deployed, working, session=None):
    if 'native_provenance' in manifest:
        from native_candidate import validate, version
        changes = validate(manifest, working)
        result = compare(manifest, staged, deployed, session, working_version=version(working))
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
    return compare(manifest, staged, deployed, session, working, spec, version(working))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'deploy', 'activate', 'launch'])
    for name in ['manifest', 'staged', 'deployed', 'working', 'records']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--mods-txt', type=Path,
                        help='UE4SS Mods/mods.txt; required only for explicit activation')
    parser.add_argument('--ignore-once', action='store_true')
    argv = sys.argv[1:]
    split = argv.index('--') if '--' in argv else len(argv)
    args = parser.parse_args(argv[:split])
    command = argv[split+1:]
    if command and args.action != 'launch':
        parser.error('Unexpected arguments')
    if args.ignore_once and args.action != 'launch':
        parser.error('--ignore-once applies only to one launch')
    if (args.action == 'activate') != (args.mods_txt is not None):
        parser.error('--mods-txt is required for activate and invalid for other actions')
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    current_session = game_session()
    result = inspect_candidate(manifest, args.staged, args.deployed, args.working, current_session)
    print(json.dumps(result, indent=2))
    args.records.mkdir(parents=True, exist_ok=True)
    record = args.records / datetime.now(timezone.utc).strftime('preflight-%Y%m%dT%H%M%S%fZ.json')
    record.write_text(json.dumps({'action': args.action, 'ignore_once': args.ignore_once, 'result': result}, indent=2)+'\n')
    if args.action == 'deploy':
        if result['stage_integrity_errors'] or result['working_differences'] or result['staged_version_differs_from_working']:
            raise RuntimeError('Rebuild/restage current working source before deployment')
        print(deploy(manifest, args.staged, args.deployed, args.records))
    elif args.action == 'activate':
        if (result['stage_integrity_errors'] or result['working_differences']
                or result['staged_version_differs_from_working'] or result['deployed_differences']):
            raise RuntimeError('Install the verified current candidate before activation')
        print(activate_mod(args.mods_txt, manifest['module'], args.records))
    elif args.action == 'launch':
        launch(result, command, args.ignore_once)
    else:
        return 2 if result['needs_attention'] else 0
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
