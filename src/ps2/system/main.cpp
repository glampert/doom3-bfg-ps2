// ================================================================================================
// File: main.cpp
// Brief: Own production startup, synchronous Common frames and ordered shutdown.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"

#include <idlib/precompiled.h>
#include <sifrpc.h>

static constexpr uint64 kFrameMicroseconds = 1000000U / 60U;

// The emulator test runners recognize a run by the exact text of the boot, main-loop, memory
// and shutdown lines below (markers in src/tools/scripts/run_headless.py). Rewording, reordering
// or dropping one fails every headless and UI gate, so change those markers in the same commit.
int main()
{
    // Preserve the loader's IOP and HostFs until console device bring-up is available.
    SifInitRpc(0);

    // Runner marker BOOT_BEGIN: selects which mirrored log stream to read.
    ps2::Log(ps2::LogLevel::Info, "======== PS2 Game boot begin ========\n");
    ps2::system::runtime::ConfigurePresentation();

    const auto baseline = ps2::system::heap::GetTotalStats();

    common->Init(0, nullptr, nullptr);
    if (!common->IsInitialized())
    {
        ps2::FatalError("Common startup did not complete!");
    }

    // Runner marker LOOP_READY: startup reached the frame loop.
    ps2::Log(ps2::LogLevel::Info, "Main loop ready; native game and %s console active\n",
             ps2::system::runtime::HasPresentation() ? "visible" : "headless");

    // A separate optional port config avoids executing desktop default.cfg device bindings.
    cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "exec d3bfg.cfg\n");

    while (!ps2::system::runtime::IsQuitRequested())
    {
        const uint64 frameStart = Sys_Microseconds();

        // Process console events, commands and native game work on this thread.
        common->Frame();

        const uint64 elapsed = Sys_Microseconds() - frameStart;
        if (!ps2::system::runtime::HasPresentation() && !ps2::system::runtime::IsQuitRequested() && elapsed < kFrameMicroseconds)
        {
            const uint64 remaining = kFrameMicroseconds - elapsed;
            Sys_Sleep(static_cast<int>((remaining + 999U) / 1000U));
        }
    }

    common->Shutdown();

    const auto final = ps2::system::heap::GetTotalStats();
    const auto arena = ps2::system::heap::GetArenaStats();

    // Runner MEMORY_RE parses these fields; full shutdown must recover the baseline exactly.
    ps2::Log(ps2::LogLevel::Info, "Memory baseline=%zu/%zu/%zu final=%zu/%zu/%zu peak=%zu/%zu arena=%zu\n",
             baseline.requestedBytes, baseline.backingBytes, baseline.allocationCount,
             final.requestedBytes, final.backingBytes, final.allocationCount,
             final.peakRequestedBytes, final.peakBackingBytes, arena.peakCommittedBytes);

    // Runner marker SHUTDOWN_COMPLETE: the process completed its ordered shutdown.
    ps2::Log(ps2::LogLevel::Info, "======== Shutdown complete ========\n");
    return 0;
}
