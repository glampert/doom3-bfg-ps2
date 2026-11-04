// ================================================================================================
// File: dma.h
// Brief: The display list system: owns the two per-frame chain rings in main RAM, hands draw code
//        the frame's scratchpad DisplayList (dma_list.h), kicks each chain it closes through VIF1
//        and fences on the GS FINISH. DIRECT blocks to PATH2 and VU1 batches share the chain. Also
//        the DMA addresses and source-chain tags every chain is made of (VIFcodes: vif.h).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/qwords.h"
#include "ps2/renderer/vif.h"
#include <tamtypes.h>

namespace ps2::renderer::dma
{

// ------------------------------------------------------------------------------------------------
// Addresses and tags
// ------------------------------------------------------------------------------------------------

// DMA addresses are physical: main RAM's cached, uncached and UCAB views share the low 28 bits.
inline u32 PhysicalAddress(const void * pointer)
{
    return static_cast<u32>(reinterpret_cast<uiptr>(pointer) & 0x0FFFFFFFU);
}

// The uncached view of a main-RAM pointer (the 0x20000000 segment, not UCAB): its stores reach
// memory at once, so a DMA reads them without a cache writeback.
template <typename T>
inline T * Uncached(T * pointer)
{
    return reinterpret_cast<T *>(reinterpret_cast<uiptr>(pointer) | 0x20000000U);
}

// Source-chain tag IDs (DMAtag bits 30:28).
enum class TagId : u32
{
    Cnt  = 1U, // the payload follows the tag
    Ref  = 3U, // the payload is at ADDR, then the next tag follows this one
    Call = 5U, // run the chain at ADDR, then come back (depth at most two)
    Ret  = 6U, // back to the tag after the CALL
    End  = 7U  // the chain ends
};

// QWC 15:0, ID 30:28, ADDR 62:32; no priority control, interrupt or scratchpad address.
constexpr u64 Tag(TagId id, u32 qwords, u32 address)
{
    return static_cast<u64>(qwords & 0xFFFFU) | (static_cast<u64>(id) << 28) | (static_cast<u64>(address & 0x7FFFFFFFU) << 32);
}

// Each tag stores with the two VIFcodes VIF1 takes from its upper half when the chain runs with
// TTE; a GIF chain passes zeros.
inline void StoreTag(qword_t * tag, u64 bits, u32 vif0, u32 vif1)
{
    StoreQword(tag, bits, static_cast<u64>(vif0) | (static_cast<u64>(vif1) << 32));
}

inline void StoreCntTag(qword_t * tag, u32 payloadQwords, u32 vif0, u32 vif1)
{
    StoreTag(tag, Tag(TagId::Cnt, payloadQwords, 0U), vif0, vif1);
}

inline void StoreRefTag(qword_t * tag, const void * data, u32 qwords, u32 vif0, u32 vif1)
{
    StoreTag(tag, Tag(TagId::Ref, qwords, PhysicalAddress(data)), vif0, vif1);
}

inline void StoreCallTag(qword_t * tag, const void * chain, u32 vif0, u32 vif1)
{
    StoreTag(tag, Tag(TagId::Call, 0U, PhysicalAddress(chain)), vif0, vif1);
}

inline void StoreRetTag(qword_t * tag, u32 vif0, u32 vif1)
{
    StoreTag(tag, Tag(TagId::Ret, 0U, 0U), vif0, vif1);
}

inline void StoreEndTag(qword_t * tag, u32 vif0, u32 vif1)
{
    StoreTag(tag, Tag(TagId::End, 0U, 0U), vif0, vif1);
}

// ------------------------------------------------------------------------------------------------
// The display list system
// ------------------------------------------------------------------------------------------------

// Two chain rings of 'ringBytes' each (64-byte multiple), one per in-flight frame.
PS2_COLD_FUNC bool Init(u32 ringBytes);
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

// BeginFrame picks the other ring; EndFrame kicks the frame's chain without waiting for it.
void BeginFrame();
void EndFrame();

// Kick: close the chain built so far (FLUSH, FINISH, END) and start VIF1 on it; later blocks
// start a new chain in the same ring. WaitIdle: the kicked chain has run and the GS has finished.
void Kick();
void WaitIdle();

// The frame's list: every draw command goes through it (dma_list.h).
class DisplayList;

// Internal state - exposed here for the inline functions below.
namespace detail
{
extern DisplayList g_list;
extern bool g_inFrame;
extern bool g_inFlight;
} // namespace detail

PS2_ALWAYS_INLINE DisplayList & List()
{
    PS2_AssertMsg(detail::g_inFrame, "display list access outside a frame");
    return detail::g_list;
}

// A kicked chain has not been waited for yet.
PS2_ALWAYS_INLINE bool InFlight()
{
    return detail::g_inFlight;
}

struct Stats
{
    u32 peakBytes; // most ring bytes a frame's chains occupied
    u32 kicks;     // VIF1 chains started
    u32 drains;    // full rings that forced a kick, wait and restart mid-frame
    u32 sprDrains; // scratchpad halves drained by the fromSPR DMA
};

const Stats & GetStats();

} // namespace ps2::renderer::dma
