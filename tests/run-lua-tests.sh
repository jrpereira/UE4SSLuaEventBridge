#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if command -v lua5.4 >/dev/null 2>&1; then
    lua5.4 tests/LuaHelperTests.lua
elif command -v lua >/dev/null 2>&1; then
    lua tests/LuaHelperTests.lua
elif command -v texlua >/dev/null 2>&1; then
    texlua tests/LuaHelperTests.lua
else
    echo "Lua 5.4 interpreter not found" >&2
    exit 1
fi

