#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "${build_dir}"' EXIT
generated_dir="${build_dir}/generated/UE4SSLuaEventBridge"
mkdir -p "${generated_dir}"
awk '
    BEGIN {
        print "#pragma once"
        print "namespace UE4SSLuaEventBridge {"
        print "inline constexpr char embedded_lua_api[] = R\"UE4SSLEB_LUA("
    }
    { print }
    END {
        print ")UE4SSLEB_LUA\";"
        print "}"
    }
' "${repo_root}/mod/lua/bridge_api.lua" > "${generated_dir}/EmbeddedLuaAPI.hpp"

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
