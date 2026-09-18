"""Deterministic packaging from an explicit source-to-install manifest."""
import hashlib
import json
from pathlib import Path
import re
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
from deployment_preflight import safe_file

SEMVER = re.compile(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?\Z')

def valid_version(value):
    match = SEMVER.fullmatch(value)
    return bool(match) and all(not (part.isdigit() and len(part) > 1 and part[0] == '0') for part in (match[4] or '').split('.'))

def definition(root):
    return json.loads((root / 'release-manifest.json').read_text(encoding='utf-8'))

def version(root):
    spec = definition(root)
    source = safe_file(root, spec['version_file']).read_text(encoding='utf-8-sig')
    match = re.search(spec['version_pattern'], source)
    if not match or not valid_version(match[1]):
        raise ValueError('Missing or invalid semantic version')
    value = match[1]
    if spec.get('metadata_version_file'):
        metadata = safe_file(root, spec['metadata_version_file']).read_text(encoding='utf-8-sig')
        declared = re.search(r'^Version\s*=\s*(\S+)', metadata, re.M)
        if not declared or declared[1] != value:
            raise ValueError('Metadata and source version disagree')
    return value

def build(root, out=None, expected=None):
    root = Path(root)
    spec = definition(root)
    value = version(root)
    if expected is not None and expected != value:
        raise ValueError('Release version does not match source version')
    module = spec['module']
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*', module):
        raise ValueError('Invalid module name')
    payload = {}
    for dest, src in spec['files'].items():
        safe_file(root, dest)
        if Path(dest).name.lower() == 'config.ini':
            raise ValueError('Never package a personal config destination; use config.example.ini')
        payload[dest] = safe_file(root, src).read_bytes()
    hashes = {p: hashlib.sha256(data).hexdigest() for p, data in sorted(payload.items())}
    manifest = {'module': module, 'version': value, 'files': hashes}
    if 'manifest.json' in payload:
        raise ValueError('manifest.json is generated, not a source payload entry')
    payload['manifest.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    out = Path(out) if out else root / 'dist'
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'{module}-v{value}.zip'
    with ZipFile(archive, 'w', compression=ZIP_DEFLATED) as bundle:
        for name, data in sorted(payload.items()):
            info = ZipInfo(f'{module}/{name}', (2020, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    return archive
