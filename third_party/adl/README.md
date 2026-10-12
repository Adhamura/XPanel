# AMD Display Library (ADL) headers

Only the headers `adl_sdk.h`, `adl_defines.h` and `adl_structures.h` are vendored here (in `include/`).

- Source: https://github.com/GPUOpen-LibrariesAndSDKs/display-library (`include/`)
- Commit: 84c881cb2136f04287eb3af12de00a9e2867aa9e (2025-04-11), downloaded 2026-10-12
- License: MIT (see the header of each file, Copyright (c) AMD)

XPanel never links ADL. `src/AmdAdlWin.cpp` loads `atiadlxx.dll` from System32 at run time
with LoadLibrary and GetProcAddress, and skips silently when it is missing.
