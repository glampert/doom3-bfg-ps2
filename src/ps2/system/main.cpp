// ================================================================================================
// File: main.cpp
// Brief: Own production startup, synchronous Common frames and ordered shutdown.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include <idlib/precompiled.h>
#include <sifrpc.h>

namespace
{
static bool s_quitRequested = false;
static constexpr uint64 kFrameMicroseconds = 1000000U / 60U;
} // namespace

namespace ps2::runtime
{
void RequestQuit() { s_quitRequested = true; }
} // namespace ps2::runtime

int main()
{
    // Preserve the loader's IOP and HostFs until console device bring-up is available.
    SifInitRpc(0);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] BOOT production BEGIN\n");
    common->Init(0, nullptr, nullptr);
    if (!common->IsInitialized())
    {
        ps2::FatalError("production Common startup did not complete");
    }
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] LOOP ready; Common services active, map simulation deferred\n");
    // A separate optional port config avoids executing desktop default.cfg device bindings.
    cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "exec d3bfg.cfg\n");

    while (!s_quitRequested)
    {
        const uint64 frameStart = Sys_Microseconds();
        // Common currently pumps commands and offline services. Map transitions, input,
        // native game ticks and presentation will join this synchronous frame boundary.
        common->Frame();
        const uint64 elapsed = Sys_Microseconds() - frameStart;
        if (!s_quitRequested && elapsed < kFrameMicroseconds)
        {
            const uint64 remaining = kFrameMicroseconds - elapsed;
            Sys_Sleep(static_cast<int>((remaining + 999U) / 1000U));
        }
    }

    common->Shutdown();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] BOOT production shutdown complete\n");
    return 0;
}
