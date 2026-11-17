// ================================================================================================
// File: iop_boot.h
// Brief: Find the package (the ELF's folder, with base/, irx/ and presentation.cfg) on the emulator's
//        host:, an APA/PFS hard disk or USB mass storage, and bring the IOP up to read it. Every IOP
//        module the game runs is started here: the storage drivers from the boot data, the rest as
//        loose files in the package's irx/, so none of them stays in EE memory.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::system::iop
{

// Finds the package and returns its root: "host:", or "pfs0:" or "mass0:".."mass9:" plus a folder.
// host: (the emulator's) is probed first, on the IOP as the loader left it. Any other boot resets
// the IOP, since what a launcher left running is no base to build on, and starts the embedded
// iomanX and fileXio, then the hard disk's drivers (unless the ELF was launched from USB), then
// USB's. The ELF's folder (from the loader's argv[0], null if none) is searched before the drive's
// root: on the launch partition, then the other PFS partitions, then every USB volume for up to 10
// seconds. A found hard disk partition stays mounted at pfs0:. Hands the boot data to the heap
// before returning. Fatal when no package is found. Call once, first in main after SifInitRpc.
PS2_COLD_FUNC const char * Boot(const char * elfPath);

// Prints what Boot did, a line per call, when it reset the IOP: the engine console is the only
// record a console has. A host: boot logged it all to the emulator already.
PS2_COLD_FUNC void PrintBootReport(void (*printLine)(const char * line));

// The drivers started after the boot, each brought up once: later calls return the first result.
// False leaves the caller without that device. Starting the other EE libraries (libkbd's
// PS2KbdInit) and opening the device stay with the caller.

// The ROM's SIO2MAN and PADMAN, then libpad's padInit.
PS2_COLD_FUNC bool StartPadDriver();

// usbd (running already when the package is on USB) and ps2kbd, from the package's irx/, for libkbd.
PS2_COLD_FUNC bool StartKeyboardDriver();

} // namespace ps2::system::iop
