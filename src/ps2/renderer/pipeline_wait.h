// ================================================================================================
// File: pipeline_wait.h
// Brief: Spin on GS/DMA completion; assertion builds bound each wait, independently of engine clocks.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/system/log.h"
#include <ee_regs.h>
#include <gs_privileged.h>
#include <tamtypes.h>

namespace ps2::renderer::gs
{

inline u32 ReadCycles()
{
    u32 cycles;
    asm volatile("mfc0 %0, $9" : "=r"(cycles));
    return cycles;
}

template<typename Ready>
void WaitUntil(Ready ready, const char * name)
{
#if PS2_D3BFG_ASSERTS
    // One second is comfortably below Count's wrap period. Unsigned subtraction also
    // handles a wait that crosses the wrap; no timer service must be alive for cleanup.
    constexpr u32 kTimeoutCycles = 294912000U;
    const u32 start = ReadCycles();
    while (!ready())
    {
        if (ReadCycles() - start >= kTimeoutCycles) [[unlikely]]
        {
            FatalError("GS pipeline timeout: %s; D1_CHCR=%08x D1_TADR=%08x VIF1_STAT=%08x VIF1_CODE=%08x GIF_STAT=%08x CSR=%08llx",
                       name, *R_EE_D1_CHCR, *R_EE_D1_TADR, *R_EE_VIF1_STAT, *R_EE_VIF1_CODE, *R_EE_GIF_STAT, *GS_REG_CSR);
        }
    }
#else // PS2_D3BFG_ASSERTS
    // A hung GS or DMA channel hangs a release build here.
    (void)name;
    while (!ready())
    {
    }
#endif // PS2_D3BFG_ASSERTS
}

} // namespace ps2::renderer::gs
