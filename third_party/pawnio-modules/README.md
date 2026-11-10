# PawnIO.Modules: RyzenSMU.bin

Signed `RyzenSMU.bin` from [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules) release 0.2.11
(`release_0_2_11.zip`), unchanged. License: LGPL-2.1 (`LICENSE`); source: `RyzenSMU.p` in that repository.

xpanel-daemon.exe loads it into the PawnIO driver (installed separately, https://pawnio.eu) to send the
AMD SMU "force GFX clock" message (PSMU 0x89) on Phoenix / Phoenix2 / Hawk Point (the Windows GPU clock).
