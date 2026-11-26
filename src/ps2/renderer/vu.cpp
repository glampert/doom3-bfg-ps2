// ================================================================================================
// File: vu.cpp
// Brief: The VU1 program manager: lays out micro memory into a resident region, one swap region and
//        the clipper library at its fixed address, prebuilds each program's MPG chain in main memory
//        once, and hands the display list that chain to CALL only when a Bind finds the program
//        absent, so a frame sorted by program swaps a few times, never per batch.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/vu.h"
#include "ps2/renderer/dma.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the chain allocation
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
    bool loaded;
    qword_t * chain;  // REF tags + RET, 64-byte aligned
    u32 chainQwords;
    MicroAddr address; // resident address, or where the swap region last placed it
    bool callsClipper;
};

static Entry s_entries[static_cast<int>(Program::Count)] = {};
static Entry s_clipper = {}; // resident at kClipperAddress, uploaded with the first program that calls it
static void * s_chains = nullptr;
static u32 s_swapBase = 0;  // first instruction of the swap region
static u32 s_swapNext = 0;  // next free instruction in it; the region ends at kClipperAddress
static Stats s_stats = {};
static bool s_initialized = false;

// The program table, in Program order. Passthrough stays; the world programs take the swap region
// and call the clipper, which sits above it. A level draws with one of them, so each frame binds the
// same one and nothing uploads again; the swap region holds both.
struct Source
{
    const char * name;
    MicroCode code;
    bool resident;
    bool callsClipper;
};

Source SourceOf(Program program)
{
    switch (program)
    {
        case Program::Passthrough:      return { "passthrough",  passthrough_Code(),  true, false };
        case Program::WorldLightmapped: return { "world_lm",     world_lm_Code(),     false, true };
        case Program::WorldStrips:      return { "world_strips", world_strips_Code(), false, true };
        case Program::Count:            break;
    }

    FatalError("unknown VU1 program %d", static_cast<int>(program));
}

inline u32 ChainQwords(u32 instructions)
{
    return (instructions + kMpgInstructions - 1U) / kMpgInstructions + 1U;
}

// REF tags carrying { NOP, MPG } for each 256-instruction piece of the program, then RET.
void BuildChain(const Entry & entry, qword_t * chain, u32 address)
{
    qword_t * tag = chain;
    const u32 * code = entry.code.start;

    for (u32 done = 0; done < entry.instructions; done += kMpgInstructions)
    {
        const u32 count = std::min(kMpgInstructions, entry.instructions - done);
        dma::StoreRefTag(tag, code + done * 2U, count / 2U, dma::kVifNop, dma::VifMpg(count & 0xFFU, address + done));
        ++tag;
    }

    dma::StoreRetTag(tag, dma::kVifNop, dma::kVifNop);
    ++tag;

    PS2_Assert(static_cast<u32>(tag - chain) == entry.chainQwords);
}

// Measures 'code' into 'entry' and counts its chain; false (logged) when it cannot be uploaded.
PS2_COLD_FUNC bool Measure(const char * name, const MicroCode & code, Entry & entry, u32 & chainQwords)
{
    entry = {};
    entry.code = code;
    entry.instructions = (code.InstructionCount() + 1U) & ~1U;
    entry.chainQwords = ChainQwords(entry.instructions);
    chainQwords += entry.chainQwords;

    if ((reinterpret_cast<uiptr>(code.start) & 15U) != 0 || entry.instructions == 0U || entry.instructions > kMicroMemInstructions) [[unlikely]]
    {
        Log(LogLevel::Error, "VU1 program %s is unusable: %u instructions at %p\n", name, entry.instructions, static_cast<const void *>(code.start));
        return false;
    }

    return true;
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

    if (!Measure("clipper", clipper_Code(), s_clipper, chainQwords)) [[unlikely]]
    {
        return false;
    }

    s_clipper.resident = true;
    s_clipper.address = static_cast<MicroAddr>(kClipperAddress);

    for (int index = 0; index < static_cast<int>(Program::Count); ++index)
    {
        const Source source = SourceOf(static_cast<Program>(index));
        Entry & entry = s_entries[index];

        if (!Measure(source.name, source.code, entry, chainQwords)) [[unlikely]]
        {
            return false;
        }

        entry.resident = source.resident;
        entry.callsClipper = source.callsClipper;

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

    // The clipper's address is fixed: its callers reach it with JALR to that address. The generated
    // header's kInstructions is what vu.h checked against micro memory; this is what was linked.
    if (residentEnd + largestSwappable > kClipperAddress || kClipperAddress + s_clipper.instructions > kMicroMemInstructions) [[unlikely]]
    {
        Log(LogLevel::Error, "VU1 programs do not fit micro memory: resident %u + largest swappable %u, clipper %u at %u\n", residentEnd,
            largestSwappable, s_clipper.instructions, kClipperAddress);
        return false;
    }

    // The chains are DMA territory: built once here, written back, and never touched again.
    const size_t bytes = static_cast<size_t>(chainQwords) * 16U;
    s_chains = system::heap::TryAlloc(bytes, TAG_RENDER, 64);
    if (s_chains == nullptr) [[unlikely]]
    {
        return false;
    }

    qword_t * chain = static_cast<qword_t *>(s_chains);
    s_clipper.chain = chain;
    BuildChain(s_clipper, chain, kClipperAddress);
    chain += s_clipper.chainQwords;

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

    Log(LogLevel::Info, "[D3BFG] VU1 programs: %d, resident %u instructions, swap region %u, clipper %u at %u\n",
        static_cast<int>(Program::Count), residentEnd, kClipperAddress - residentEnd, s_clipper.instructions, kClipperAddress);
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

    s_clipper = {};
    s_initialized = false;
}

bool IsInitialized()
{
    return s_initialized;
}

Binding Bind(Program program)
{
    PS2_AssertMsg(s_initialized && static_cast<int>(program) < static_cast<int>(Program::Count), "invalid VU1 program bind");

    Entry & entry = s_entries[static_cast<int>(program)];
    Binding binding = { entry.address, nullptr, nullptr };

    if (entry.callsClipper && !s_clipper.loaded)
    {
        s_clipper.loaded = true;
        ++s_stats.libraryUploads;
        binding.library = s_clipper.chain;
    }

    if (entry.loaded)
    {
        return binding;
    }

    if (!entry.resident)
    {
        // Bump allocation in the swap region; a program that does not fit behind the loaded ones
        // evicts them all. With one or two programs per frame phase this is the LRU it needs.
        if (s_swapNext + entry.instructions > kClipperAddress)
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
        // address, through the uncached view since the DMA reads what main memory holds. A chain in
        // flight (the last frame's, pipelined) may still CALL it with the old codes: it finishes first.
        dma::WaitIdle();
        qword_t * tag = dma::Uncached(entry.chain);
        for (u32 done = 0; done < entry.instructions; done += kMpgInstructions, ++tag)
        {
            const u32 count = std::min(kMpgInstructions, entry.instructions - done);
            tag->sw[3] = dma::VifMpg(count & 0xFFU, static_cast<u32>(entry.address) + done);
        }
    }

    entry.loaded = true;
    ++s_stats.uploads;
    binding.entry = entry.address;
    binding.upload = entry.chain;
    return binding;
}

void Invalidate()
{
    for (Entry & entry : s_entries)
    {
        entry.loaded = false;
    }

    s_clipper.loaded = false;
    s_swapNext = s_swapBase;
}

const Stats & GetStats()
{
    return s_stats;
}

} // namespace ps2::renderer::vu
