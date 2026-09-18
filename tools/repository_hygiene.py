"""Reject local/generated files in Git trees, the index, and introduced commits.

Uses Git objects rather than working files so staged and historical checks cannot
be fooled by deleting or changing a file after staging it. No third-party deps.
"""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

FORBIDDEN_DIRS = {"build", "dist", "artifacts", "tmp", "temp", "work", "__pycache__", ".vs", ".venv", "logs", "dumps", "reports", "audits", "internal"}
FORBIDDEN_SUFFIXES = {".zip", ".7z", ".rar", ".dll", ".exe", ".pdb", ".ilk", ".dmp", ".dump", ".mdmp", ".log", ".etl", ".trace", ".bak", ".pyc", ".suo", ".user"}
SECRET = re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")

def violations(path, data, allowed=()):
    p = PurePosixPath(path)
    if path in allowed:
        # Exceptions relax path rules only, never private-key checks.
        result = []
    else:
        parts = [s.lower() for s in p.parts]
        name = parts[-1]
        result = []
        if any(s in {'diagnostics', 'evidence'} for s in parts[:-1]) or name in {'runtime_evidence.py', 'test_runtime_evidence.py', 'reconstructed_history.json'}:
            result.append('diagnostic tooling and evidence belong outside public source')
        if any(s in FORBIDDEN_DIRS for s in parts[:-1]):
            result.append("generated/local output directory")
        if path.lower().startswith('benchmarks/runner/samples/'):
            result.append('collected benchmark session data belongs in internal records')
        if p.suffix.lower() in FORBIDDEN_SUFFIXES or ".bak." in name:
            result.append("generated archive, binary, log or backup")
        if name == "config.ini" or name.startswith(".env") and name not in {".env.example", ".env.sample"}:
            result.append("personal configuration")
        if p.suffix.lower() in {".bat", ".cmd"}:
            result.append("batch file requires an explicit reviewed exception")
        audit_document = name.startswith(('audit_', 'audit-', 'final_audit')) and p.suffix.lower() in {'.md','.txt','.json','.csv','.html','.pdf'}
        if audit_document or name.startswith('crashcontext.') or name in {'coordination.md', 'handoff.json', 'crashreportclient.ini', 'continuation.md', 'current-model.md', 'sample_runs.md', 'inventory_ui_route.md', 'weak_serial_fix.md', 'dev-setup.md'}:
            result.append("internal coordination, audit or crash record")
    if SECRET.search(data):
        result.append("private key material")
    return result

def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])

def entries(root, revision):
    if revision == ":index":
        raw = git(root, "ls-files", "--stage", "-z")
    else:
        raw = git(root, "ls-tree", "-rz", revision)
    for record in raw.split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        bits = meta.split()
        if revision == ":index":
            mode, oid, stage = bits
            if stage != b"0":
                raise ValueError("Resolve index conflicts before checking hygiene")
        else:
            mode, kind, oid = bits
            if kind != b"blob":
                continue
        yield path.decode("utf-8", "surrogateescape"), oid.decode("ascii")

def policy_at(root, revision):
    objects = dict(entries(root, revision))
    oid = objects.get('repository-policy.json')
    if oid is None:
        # Pre-policy history already used this reviewed public-default location.
        # This fixed exception never permits root or nested personal configs.
        return ['distribution/config.ini']
    return json.loads(git(root, 'cat-file', 'blob', oid))['allowed_paths']

def blobs(root, object_ids):
    ids = sorted(set(object_ids))
    if not ids:
        return {}
    raw = subprocess.check_output(['git','-C',str(root),'cat-file','--batch'],
                                  input=('\n'.join(ids)+'\n').encode('ascii'))
    result, cursor = {}, 0
    for oid in ids:
        end = raw.index(b'\n', cursor)
        found, kind, size = raw[cursor:end].split()
        if found.decode() != oid or kind != b'blob':
            raise ValueError('Unexpected Git object response')
        cursor = end + 1
        length = int(size)
        result[oid] = raw[cursor:cursor+length]
        cursor += length + 1
    return result

def check(root, revisions, allowed=None):
    failures = []
    seen = set()
    trees = {rev: dict(entries(root, rev)) for rev in revisions}
    policies = blobs(root, [tree['repository-policy.json'] for tree in trees.values() if 'repository-policy.json' in tree])
    pending = []
    for revision, tree in trees.items():
        revision_allowed = allowed
        if revision_allowed is None:
            policy = tree.get('repository-policy.json')
            revision_allowed = json.loads(policies[policy])['allowed_paths'] if policy else ['distribution/config.ini']
        for path, oid in tree.items():
            key = (path, oid, tuple(sorted(revision_allowed)))
            if key in seen:
                continue
            seen.add(key)
            reasons = violations(path, b'', revision_allowed)
            for reason in reasons:
                failures.append(f"{revision}: {path}: {reason}")
            if not reasons:
                pending.append((revision, path, oid, revision_allowed))
    contents = blobs(root, [item[2] for item in pending])
    for revision, path, oid, revision_allowed in pending:
        for reason in violations(path, contents[oid], revision_allowed):
            failures.append(f"{revision}: {path}: {reason}")
    return failures

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--staged", action="store_true")
    group.add_argument("--range", dest="commit_range")
    group.add_argument("--revision", default="HEAD")
    args = parser.parse_args()
    if args.staged:
        revisions = [":index"]
    elif args.commit_range:
        revisions = git(args.root, "rev-list", args.commit_range).decode().splitlines()
    else:
        revisions = [args.revision]
    failures = check(args.root, revisions)
    print("\n".join(failures) if failures else "Repository hygiene passed")
    return int(bool(failures))

if __name__ == "__main__":
    raise SystemExit(main())
