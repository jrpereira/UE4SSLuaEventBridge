#!/usr/bin/env bash
# Portable C++ tests of the file bridge core (no UE4SS, no Win32).
set -euo pipefail

product_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "${build_dir}"' EXIT

for test in CodecTests PathTests PolicyTests AtomicWriteTests TailTests; do
    c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
        -I"${product_root}/include" \
        "${product_root}/tests/${test}.cpp" \
        -o "${build_dir}/${test}"
    "${build_dir}/${test}"
done
