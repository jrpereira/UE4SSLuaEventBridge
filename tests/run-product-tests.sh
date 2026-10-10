#!/usr/bin/env bash
# Runs each product's own portable runners (<product>/tests/run-*.sh), if any.
# Products without such runners are covered by the repository-level runners.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shopt -s nullglob
for runner in "${repo_root}"/bridge-*/tests/run-*.sh; do
    echo "Running ${runner#"${repo_root}/"}"
    bash "${runner}"
done
