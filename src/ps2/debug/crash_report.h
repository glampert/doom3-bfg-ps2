// ================================================================================================
// File: crash_report.h
// Brief: Assertion builds turn a fatal error or an EE CPU exception into a report on stdout and on
//        a red debug screen, with the unwound call stack, and halt there: a console has no other
//        record. Without the exception handler, a bad pointer gives a hang and a few emulator lines
//        that name neither the function nor the call that reached it.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstdarg>

#if PS2_D3BFG_ASSERTS

namespace ps2::debug
{

// Installs the level 1 CPU exception handlers (ps2sdk's libeedebug, which replaces the EE's level 1
// vectors). Call once, first in every EE main after SifInitRpc.
PS2_COLD_FUNC void InstallExceptionHandlers();

// ps2::FatalErrorV's assertion-build end, after it logged the message: logs the call stack, draws
// the message and the stack on the debug screen, and halts. A fatal error while reporting one only
// halts.
[[noreturn]] PS2_COLD_FUNC void ReportFatalError(const char * format, va_list arguments);

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
