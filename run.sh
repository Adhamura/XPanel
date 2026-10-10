#!/usr/bin/env bash
# XPanel runner: builds when the sources are newer than the binary, then starts XPanel.
# Usage: ./run.sh [--toggle|--open|--close|--quit|--background|--version]
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
BIN=build/bin/XPanel
if [ ! -x "$BIN" ] || [ -n "$(find src qml daemon CMakeLists.txt -newer "$BIN" -print -quit)" ]; then
    echo "Building XPanel..."
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build
fi
exec "$BIN" "$@"
