"""Check every incoming commit, including files deleted before the final tree."""
import json
import os
from pathlib import Path
import subprocess
import sys
from repository_hygiene import check, git

root = Path.cwd()


def known_commit(sha):
    """Whether sha names a commit present in this checkout (not zero, not force-pushed away)."""
    if not sha or set(sha) == {'0'}:
        return False
    return subprocess.run(['git', '-C', str(root), 'cat-file', '-e', f'{sha}^{{commit}}'],
                          capture_output=True).returncode == 0


def incoming(before, after):
    # A new branch has a zero before SHA, and after a force-push the old tip is not
    # in the checkout. Both inspect the complete lineage of the new tip.
    return git(root, 'rev-list', f'{before}..{after}' if known_commit(before) else after).decode().splitlines()


revisions = [] if '--pre-push' in sys.argv else ['HEAD']
if '--pre-push' in sys.argv:
    for line in sys.stdin:
        local_ref, local_sha, remote_ref, remote_sha = line.split()
        if set(local_sha) == {'0'}:
            continue
        # Another remote knowing a commit does not prove this destination
        # already contains it. Inspect all ancestry for a new target ref.
        revisions.extend(incoming(remote_sha, local_sha))
else:
    event_path = os.environ.get('GITHUB_EVENT_PATH')
    if event_path:
        event = json.loads(Path(event_path).read_text(encoding='utf-8'))
        if 'pull_request' in event:
            base = event['pull_request']['base']['sha']
            head = event['pull_request']['head']['sha']
            revisions.extend(git(root, 'rev-list', f'{base}..{head}').decode().splitlines())
        elif event.get('after') and set(event['after']) != {'0'}:
            revisions.extend(incoming(event.get('before', ''), event['after']))
failures = check(root, list(dict.fromkeys(revisions)))
print('\n'.join(failures) if failures else 'Incoming history hygiene passed')
raise SystemExit(bool(failures))
