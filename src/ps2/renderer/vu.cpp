// ================================================================================================
// File: vu.cpp
// Brief: The VU1 program manager: lays out micro memory into a resident region and one swap region,
//        prebuilds each program's MPG chain in main memory once, and emits a CALL into that chain
//        only when a Bind finds the program absent, so a frame sorted by program swaps a few times,
//        never per batch (TimeSplitters' program packages, Black's .vutext chains).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/vu.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/qwords.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the chain allocation
#include <dma_tags.h>
#include <gif_tags.h>
#include <kernel.h>

#include <algorithm>

namespace ps2::renderer::vu
{
namespace
{

// One MPG moves at most 256 instructions (128 qwords); a REF tag per piece, then RET.
static constexpr u32 kMpgInstructions = 256U;

struct Entry
{
    MicroCode code;
    u32 instructions; // rounded up to even: MPG moves whole qwords
    bool resident;
    qword_t * chain;  // REF tags + RET, 64-byte aligned
    u32 chainQwords;
    MicroAddr address; // resident address, or where the swap region last placed it
    bool loaded;
};

static Entry s_entries[static_cast<int>(Program::Count)] = {};
static void * s_chains = nullptr;
static u32 s_swapBase = 0;  // first instruction of the swap region
static u32 s_swapNext = 0;  // next free instruction in it
static Stats s_stats = {};
static bool s_initialized = false;

// The program table, in Program order. Residency follows the plan: the kernel, the lightmapped
// world program and passthrough stay; everything else takes the swap region.
struct Source
{
    MicroCode code;
    bool resident;
};

Source SourceOf(Program program)
{
    switch (program)
    {
        case Program::Passthrough: return { passthrough_Code(), true };
        case Program::Count: break;
    }

    FatalError("unknown VU1 program %d", static_cast<int>(program));
}

inline u32 PhysicalAddress(const void * pointer)
{
    return static_cast<u32>(reinterpret_cast<uiptr>(pointer) & 0x0FFFFFFFU);
}

inline u32 ChainQwords(u32 instructions)
{
    return (instructions + kMpgInstructions - 1U) / kMpgInstructions + 1U;
}

// REF tags carrying { NOP, MPG } for each 256-instruction piece of the program, then RET.
void BuildChain(Entry & entry, qword_t * chain, u32 address)
{
    qword_t * tag = chain;
    const u32 * code = entry.code.start;
    for (u32 done = 0; done < entry.instructions; done += kMpgInstructions)
    {
        const u32 count = std::min(kMpgInstructions, entry.instructions - done);
        StoreQword(tag, DMATAG(count / 2U, 0, DMA_TAG_REF, 0, PhysicalAddress(code + done * 2U), 0),
                   static_cast<u64>(dma::kVifNop) | (static_cast<u64>(dma::VifMpg(count & 0xFFU, address + done)) << 32));
        ++tag;
    }

    StoreQword(tag, DMATAG(0, 0, DMA_TAG_RET, 0, 0, 0), static_cast<u64>(dma::kVifNop) | (static_cast<u64>(dma::kVifNop) << 32));
    ++tag;
    PS2_Assert(static_cast<u32>(tag - chain) == entry.chainQwords);
}

} // namespace

PS2_COLD_FUNC bool Init()
{
    if (s_initialized) [[unlikely]]
    {
        FatalError("VU program manager initialized twice");
    }

    u32 residentEnd = 0;
    u32 largestSwappable = 0;
    u32 chainQwords = 0;
    for (int index = 0; index < static_cast<int>(Program::Count); ++index)
    {
        const Source source = SourceOf(static_cast<Program>(index));
        Entry & entry = s_entries[index];
        entry = {};
        entry.code = source.code;
        entry.instructions = (source.code.InstructionCount() + 1U) & ~1U;
        entry.resident = source.resident;
        entry.chainQwords = ChainQwords(entry.instructions);
        chainQwords += entry.chainQwords;

        if ((reinterpret_cast<uiptr>(source.code.start) & 15U) != 0 || entry.instructions == 0U ||
            entry.instructions > kMicroMemInstructions)
        {
            Log(LogLevel::Error, "VU1 program %d is unusable: %u instructions at %p\n", index, entry.instructions, static_cast<const void *>(source.code.start));
            return false;
        }

        if (entry.resident)
        {
            entry.address = static_cast<MicroAddr>(residentEnd);
            residentEnd += entry.instructions;
        }
        else
        {
            largestSwappable = std::max(largestSwappable, entry.instructions);
        }
    }

    if (residentEnd + largestSwappable > kMicroMemInstructions)
    {
        Log(LogLevel::Error, "VU1 programs do not fit micro memory: resident %u + largest swappable %u\n", residentEnd, largestSwappable);
        return false;
    }

    // The chains are DMA territory: built once here, written back, and never touched again.
    const size_t bytes = static_cast<size_t>(chainQwords) * 16U;
    s_chains = system::heap::TryAlloc(bytes, TAG_RENDER, 64);
    if (s_chains == nullptr)
    {
        return false;
    }

    qword_t * chain = static_cast<qword_t *>(s_chains);
    for (int index = 0; index < static_cast<int>(Program::Count); ++index)
    {
        Entry & entry = s_entries[index];
        entry.chain = chain;
        BuildChain(entry, chain, entry.resident ? static_cast<u32>(entry.address) : 0U);
        chain += entry.chainQwords;
    }

    SyncDCache(s_chains, static_cast<u8 *>(s_chains) + bytes);

    s_swapBase = residentEnd;
    s_swapNext = residentEnd;
    s_stats = {};
    s_initialized = true;

    Log(LogLevel::Info, "[D3BFG] VU1 programs: %d, resident %u instructions, swap region %u\n", static_cast<int>(Program::Count), residentEnd,
        kMicroMemInstructions - residentEnd);
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    system::heap::Free(s_chains);
    s_chains = nullptr;
    for (Entry & entry : s_entries)
    {
        entry = {};
    }

    s_initialized = false;
}

bool IsInitialized()
{
    return s_initialized;
}

MicroAddr Bind(dma::DisplayList & list, Program program)
{
    PS2_AssertMsg(s_initialized && static_cast<int>(program) < static_cast<int>(Program::Count), "invalid VU1 program bind");

    Entry & entry = s_entries[static_cast<int>(program)];
    if (entry.loaded)
    {
        return entry.address;
    }

    if (!entry.resident)
    {
        // Bump allocation in the swap region; a program that does not fit behind the loaded ones
        // evicts them all. With one or two programs per frame phase this is the LRU it needs.
        if (s_swapNext + entry.instructions > kMicroMemInstructions)
        {
            for (Entry & other : s_entries)
            {
                other.loaded = other.loaded && other.resident;
            }

            s_swapNext = s_swapBase;
            ++s_stats.evictions;
        }

        entry.address = static_cast<MicroAddr>(s_swapNext);
        s_swapNext += entry.instructions;

        // The prebuilt chain carries the destination in its MPG codes; rewrite them for the new
        // address, through the uncached view since the DMA reads what main memory holds.
        qword_t * tag = reinterpret_cast<qword_t *>(reinterpret_cast<uiptr>(entry.chain) | 0x20000000U);
        for (u32 done = 0; done < entry.instructions; done += kMpgInstructions, ++tag)
        {
            const u32 count = std::min(kMpgInstructions, entry.instructions - done);
            tag->sw[3] = dma::VifMpg(count & 0xFFU, static_cast<u32>(entry.address) + done);
        }
    }

    list.Call(entry.chain, dma::kVifNop, dma::kVifNop);
    entry.loaded = true;
    ++s_stats.uploads;
    return entry.address;
}

void Invalidate()
{
    for (Entry & entry : s_entries)
    {
        entry.loaded = false;
    }

    s_swapNext = s_swapBase;
}

void EmitFrameConstants(dma::DisplayList & list)
{
    PS2_AssertMsg(s_initialized, "VU1 frame constants before initialization");

    // STCYCL 1,1 then one absolute V4-32 unpack: the null GIF tag passthrough kicks as its drain guard.
    qword_t * const payload = list.Cnt(1U, dma::VifStcycl(1U, 1U), dma::VifUnpack(dma::UnpackFormat::V4_32, 1U, kNullGifTagAddr, false, false));
    PACK_GIFTAG(payload, GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 0), 0);
}

const Stats & GetStats()
{
    return s_stats;
}

} // namespace ps2::renderer::vu
