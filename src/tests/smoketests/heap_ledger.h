// ================================================================================================
// File: heap_ledger.h
// Brief: The heap ledger a check of one tag compares. Tracking builds keep a ledger per tag and exact
//        requested sizes; release builds keep only the total and count backing bytes as requested.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/system/heap.h"

namespace ps2::tests
{

// Exact requested-size and per-tag count checks hold in tracking builds only.
static constexpr bool kExactHeapLedgers = PS2_D3BFG_ASSERTS != 0;

inline system::heap::Stats TagLedger(std::uint16_t tag)
{
#if PS2_D3BFG_ASSERTS
    return system::heap::GetStats(tag);
#else // PS2_D3BFG_ASSERTS
    static_cast<void>(tag);
    return system::heap::GetTotalStats();
#endif // PS2_D3BFG_ASSERTS
}

inline bool SameLedger(const system::heap::Stats & first, const system::heap::Stats & second)
{
    return first.requestedBytes == second.requestedBytes && first.backingBytes == second.backingBytes &&
           first.allocationCount == second.allocationCount;
}

} // namespace ps2::tests
