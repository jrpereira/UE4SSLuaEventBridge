#!/usr/bin/env bash
set -euo pipefail

c++ -std=c++20 -Wall -Wextra -Werror -pthread \
    -Imod/include \
    mod/src/EventRegistry.cpp \
    tests/EventRegistryTests.cpp \
    -o /tmp/ue4ss-lua-event-bridge-tests

/tmp/ue4ss-lua-event-bridge-tests

