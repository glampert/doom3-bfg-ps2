// ================================================================================================
// File: main.cpp
// Brief: Own production startup, synchronous Common frames and ordered shutdown.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include "ps2/system/heap.h"
#include "ps2/system/runtime.h"
#include <idlib/precompiled.h>
#include <sifrpc.h>

namespace
{
static constexpr uint64 kFrameMicroseconds = 1000000U / 60U;
} // namespace

int main()
{
    // Preserve the loader's IOP and HostFs until console device bring-up is available.
    SifInitRpc(0);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] BOOT production BEGIN\n");
    ps2::runtime::ConfigurePresentation();
    const auto baseline = ps2::heap::GetTotalStats();
    common->Init(0, nullptr, nullptr);
    if (!common->IsInitialized())
    {
        ps2::FatalError("production Common startup did not complete");
    }
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] LOOP ready; native game and %s console active\n",
             ps2::runtime::HasPresentation() ? "visible" : "headless");
    // A separate optional port config avoids executing desktop default.cfg device bindings.
    cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "exec d3bfg.cfg\n");

    while (!ps2::runtime::IsQuitRequested())
    {
        const uint64 frameStart = Sys_Microseconds();
        // Process console events, commands and native game work on this thread.
        common->Frame();
        const uint64 elapsed = Sys_Microseconds() - frameStart;
        if (!ps2::runtime::HasPresentation() && !ps2::runtime::IsQuitRequested() && elapsed < kFrameMicroseconds)
        {
            const uint64 remaining = kFrameMicroseconds - elapsed;
            Sys_Sleep(static_cast<int>((remaining + 999U) / 1000U));
        }
    }

    common->Shutdown();
    const auto final = ps2::heap::GetTotalStats();
    const auto arena = ps2::heap::GetArenaStats();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] HEADLESS memory baseline=%zu/%zu/%zu final=%zu/%zu/%zu peak=%zu/%zu arena=%zu\n",
             baseline.requestedBytes, baseline.backingBytes, baseline.allocationCount,
             final.requestedBytes, final.backingBytes, final.allocationCount,
             final.peakRequestedBytes, final.peakBackingBytes, arena.peakCommittedBytes);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] BOOT production shutdown complete\n");
    return 0;
}
