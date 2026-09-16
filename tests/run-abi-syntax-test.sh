#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "${build_dir}"' EXIT
generated_dir="${build_dir}/generated/UE4SSLuaEventBridge"
mkdir -p "${generated_dir}"
header="${generated_dir}/EmbeddedLuaAPI.hpp"
printf '%s\n' \
    '#pragma once' \
    '#include <array>' \
    '#include <string_view>' \
    'namespace UE4SSLuaEventBridge {' \
    'inline constexpr std::array<std::string_view, 3> embedded_lua_api_chunks{' \
    > "${header}"
for offset in 0 7000 14000; do
    printf '%s' 'R"UE4SSLEB_LUA(' >> "${header}"
    dd if="${repo_root}/mod/lua/bridge_api.lua" bs=1 skip="${offset}" count=7000 \
        status=none >> "${header}"
    printf '%s\n' ')UE4SSLEB_LUA",' >> "${header}"
done
printf '%s\n' '};' '}' >> "${header}"

g++ \
    -std=c++20 \
    -D_WIN32 \
    '-D__declspec(x)=' \
    -I"${build_dir}/generated" \
    -I"${repo_root}/mod/include" \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -fsyntax-only \
    "${repo_root}/mod/src/BridgeMod.cpp" \
    "${repo_root}/mod/src/EnhancedInputBackend.cpp"
