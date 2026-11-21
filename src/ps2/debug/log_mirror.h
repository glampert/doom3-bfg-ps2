// ================================================================================================
// File: log_mirror.h
// Brief: The log for a console run, where no emulator reads stdout: every ps2::LogV line also goes
//        to the screen while the boot has it (from the first line of main until the renderer takes
//        the GS, or the main loop starts) and to d3bfg.log in the package folder, with the lines
//        logged before the boot found the drive written first. A hang leaves its last line on the
//        screen, or in the file once the drive is up. Assertion builds only.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/system/log.h"

namespace ps2::debug
{

#if PS2_D3BFG_ASSERTS

// Takes the GS over with the crash screen's printer (scr_print.h) and prints every log line on it.
PS2_COLD_FUNC void BeginBootScreen();

// Stops printing on the screen, which keeps what it shows until the GS is set up again. Called
// before the renderer drives the GIF itself, and once the main loop starts.
PS2_COLD_FUNC void EndBootScreen();

// Points the log file at "<packageRoot>/d3bfg.log", emptied, and writes out what was logged until
// now. host: turns the file off: the emulator keeps stdout. A write that fails turns it off too.
PS2_COLD_FUNC void OpenLogFile(const char * packageRoot);

// One LogV message, prefix and line end included. Each write appends and closes the file, since
// closing is the only flush a USB drive's FatFs has.
void MirrorLog(LogLevel level, const char * text);

#else

inline void BeginBootScreen()
{
}

inline void EndBootScreen()
{
}

inline void OpenLogFile(const char *)
{
}

#endif // PS2_D3BFG_ASSERTS

} // namespace ps2::debug
