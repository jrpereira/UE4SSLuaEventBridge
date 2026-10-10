#!/usr/bin/env bash
# Runs the UE4SSLuaFileBridge Lua-layer tests against the fake natives.
# Lua 5.4 only: the layer relies on 5.4 integers, bitwise operators and __gc on tables.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${repo_root}"

if ! command -v lua5.4 >/dev/null 2>&1; then
    echo "lua5.4 not found" >&2
    exit 1
fi

for test_file in bridge-files/tests/*Tests.lua; do
    lua5.4 "${test_file}"
done
