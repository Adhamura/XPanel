#pragma once

// AMD SMU access on Windows through PawnIO (https://pawnio.eu), with the signed RyzenSMU.bin module
// from PawnIO.Modules (LGPL-2.1, shipped next to xpanel-daemon.exe).
// Needs the PawnIO driver installed and an elevated process.
namespace RyzenSmu {

// CPU code names of RyzenSMU.bin that XPanel uses.
enum CodeName { Phoenix = 23, Phoenix2 = 24, HawkPoint = 30 };

// true when PawnIO, the module and a supported CPU (Phoenix, Phoenix2, Hawk Point) are there.
bool available();
int codeName();          // RyzenSMU.bin code name, -1 = not loaded

// Forces the iGPU to one clock in MHz (PSMU message 0x89, RyzenAdj --gfx-clk).
// The firmware keeps it until the next restart: no message gives control back to the driver.
bool forceGfxClock(int mhz);

} // namespace RyzenSmu
