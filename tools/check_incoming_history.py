"""Check every incoming commit, including files deleted before the final tree."""
import json
import os
from pathlib import Path
import sys
from repository_hygiene import check, git

root = Path.cwd()
revisions = [] if '--pre-push' in sys.argv else ['HEAD']
if '--pre-push' in sys.argv:
    for line in sys.stdin:
        local_ref, local_sha, remote_ref, remote_sha = line.split()
        if set(local_sha) == {'0'}:
            continue
        if set(remote_sha) == {'0'}:
            # Another remote knowing a commit does not prove this destination
            # already contains it. Inspect all ancestry for a new target ref.
            args = ['rev-list', local_sha]
        else:
            args = ['rev-list', f'{remote_sha}..{local_sha}']
        revisions.extend(git(root, *args).decode().splitlines())
else:
    event_path = os.environ.get('GITHUB_EVENT_PATH')
    if event_path:
        event = json.loads(Path(event_path).read_text(encoding='utf-8'))
        if 'pull_request' in event:
            base = event['pull_request']['base']['sha']
            head = event['pull_request']['head']['sha']
            revisions.extend(git(root, 'rev-list', f'{base}..{head}').decode().splitlines())
        elif event.get('after') and set(event['after']) != {'0'}:
            before, after = event.get('before', ''), event['after']
            # New branches have a zero before SHA: inspect their complete lineage.
            rev = f'{before}..{after}' if before and set(before) != {'0'} else after
            revisions.extend(git(root, 'rev-list', rev).decode().splitlines())
failures = check(root, list(dict.fromkeys(revisions)))
print('\n'.join(failures) if failures else 'Incoming history hygiene passed')
raise SystemExit(bool(failures))
