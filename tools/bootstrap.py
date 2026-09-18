"""Opt in to this repository's reviewed local Git guards; no global changes."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
subprocess.run(['git', '-C', str(root), 'config', '--local', 'core.hooksPath', '.githooks'], check=True)
print('Enabled repository pre-commit and pre-push guards. Python must be on PATH.')
