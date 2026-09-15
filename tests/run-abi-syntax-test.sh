#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

g++ \
    -std=c++20 \
    -D_WIN32 \
    '-D__declspec(x)=' \
    -I"${repo_root}/mod/include" \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -fsyntax-only \
    "${repo_root}/mod/src/BridgeMod.cpp" \
    "${repo_root}/mod/src/EnhancedInputBackend.cpp"
