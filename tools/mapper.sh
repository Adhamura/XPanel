#!/usr/bin/env bash
# XPanel Button Mapper: builds when needed, then opens the mapper window.
# The map is saved in XPanel/data/ally-x-input-map.json (+ .md table).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
if [ ! -x build/bin/xpanel-mapper ] || [ -n "$(find tools/mapper -newer build/bin/xpanel-mapper -print -quit)" ]; then
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPANEL_BUILD_TOOLS=ON >/dev/null
    cmake --build build --target xpanel-mapper xpanel-input-probe
fi
exec build/bin/xpanel-mapper "$PWD/data/ally-x-input-map.json"
