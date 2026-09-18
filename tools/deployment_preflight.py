"""On-demand payload and session checks. Never deploys, stops or launches a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

def safe_file(root, relative):
    p = PurePosixPath(relative)
    if p.is_absolute() or '..' in p.parts or '\\' in relative or ':' in relative:
        raise ValueError('Manifest paths must be relative POSIX paths')
    target = (root / relative).resolve()
    if not target.is_relative_to(root.resolve()):
        raise ValueError('Manifest path escapes payload root')
    return target

def compare(manifest, staged, deployed, session=None, working=None, definition=None, working_version=None):
    """Compare on-disk payloads; a running process prevents safe replacement."""
    changes = []
    stage_errors = []
    working_changes = []
    for relative, expected in manifest['files'].items():
        if digest(safe_file(staged, relative)) != expected:
            stage_errors.append(relative)
        # Deployment preserves enablement independently of the packaged default.
        if relative != 'enabled.txt' and digest(safe_file(deployed, relative)) != expected:
            changes.append(relative)
        if working is not None:
            source = definition['files'].get(relative) if definition else relative
            if source is None or digest(safe_file(working, source)) != expected:
                working_changes.append(relative)
    stale_version = working_version is not None and working_version != manifest['version']
    deployed_version = manifest['version'] if not changes and not stage_errors else None
    if deployed_version is None and definition:
        version_file = safe_file(deployed, definition['version_file'])
        if version_file.is_file():
            match = re.search(definition['version_pattern'], version_file.read_text(encoding='utf-8-sig'))
            deployed_version = match[1] if match else None
    needs_attention = bool(stage_errors or working_changes or stale_version or changes or session)
    return {
        'module': manifest['module'], 'intended_version': manifest['version'],
        'deployed_version': deployed_version,
        'stage_integrity_errors': stage_errors, 'deployed_differences': changes,
        'working_differences': working_changes, 'working_version': working_version,
        'staged_version_differs_from_working': stale_version,
        'game_running': bool(session), 'needs_attention': needs_attention,
        'options': ([
            'Rebuild/restage the working candidate; repeat verification.' if stage_errors or working_changes or stale_version else
            ('Close game, deploy verified candidate preserving config, then relaunch.' if session else
             'Deploy verified candidate preserving config, then launch.'),
            'Ignore this discrepancy for this launch only; retain the warning in the test record.'
        ] if needs_attention else []),
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--staged', type=Path, required=True)
    parser.add_argument('--deployed', type=Path, required=True)
    parser.add_argument('--working', type=Path, help='Working payload source; map source paths with --definition')
    parser.add_argument('--definition', type=Path, help='Source-to-install release-manifest.json')
    parser.add_argument('--working-version', required=True, help='Version read from current working source, independent of stage')
    parser.add_argument('--session', type=Path, help='Fresh process identity JSON supplied by launch adapter')
    args = parser.parse_args()
    read = lambda p: json.loads(p.read_text(encoding='utf-8')) if p else None
    session = read(args.session)
    if args.session is None and os.name == 'nt':
        command = "@(Get-Process -Name Dawnwalker -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{pid=$_.Id;started_at=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress"
        raw = subprocess.check_output(['powershell','-NoProfile','-NonInteractive','-Command',command], text=True, timeout=20).strip()
        if raw:
            sessions = json.loads(raw)
            if isinstance(sessions, list):
                if len(sessions) != 1:
                    parser.error('Multiple game processes: supply a fresh explicit session identity')
                sessions = sessions[0]
            session = sessions
    result = compare(read(args.manifest), args.staged, args.deployed, session, args.working, read(args.definition), args.working_version)
    print(json.dumps(result, indent=2))
    return 2 if result['needs_attention'] else 0

if __name__ == '__main__':
    raise SystemExit(main())
