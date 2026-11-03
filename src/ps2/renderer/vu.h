// ================================================================================================
// File: vu.h
// Brief: The VU1 microprograms: their link symbols in the ELF's .vutext section and the VU1 data
//        memory map they share with vu1progs/vu_memory.i. The program manager (upload, the swap
//        region, Bind) joins this header with the display list.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

#include <tamtypes.h>

namespace ps2::renderer::vu
{

// An assembled microprogram's extent in .vutext (dvp-as's `.vu` section; quake2-ps2's comments
// call it .vudata, whose Doom objects are empty). Instruction counts are in 64-bit VU
// instructions, the unit MPG upload destinations and MSCAL entry points use, not qwords
// (half as many).
struct MicroCode
{
    const u32 * start;
    const u32 * end;

    u32 InstructionCount() const
    {
        return static_cast<u32>(end - start) / 2u;
    }
};

// ------------------------------------------------------------------------------------------------
// VU1 data memory map (qword addresses), mirrored by vu1progs/vu_memory.i. The remaining regions
// (input halves, output windows, clip scratch, light block) arrive with the kernel.
// ------------------------------------------------------------------------------------------------

// Frame constants: MVP rows, GS scale and offset, clip scale, fog.
static constexpr u32 kFrameConstantsAddr = 0;

// Per-pass GIF tag and A+D templates. The first is a null GIF tag (NLOOP 0, EOP 1): the drain
// guard passthrough.vcl kicks so a program ends only once its packet has left VU memory.
static constexpr u32 kGifTemplatesAddr = 8;
static constexpr u32 kNullGifTagAddr   = 8;

} // namespace ps2::renderer::vu

// Declares the linker symbols bracketing an assembled microprogram and its MicroCode accessor.
// 'name' must match the #vuprog name in the .vcl. Use it at global scope: an extern "C" inside
// an anonymous namespace gets internal linkage and does not resolve against the VU object.
#define PS2_DECLARE_VU_MICROPROGRAM(name)                                                        \
    extern "C" u32 name##_CodeStart __attribute__((section(".vutext")));                          \
    extern "C" u32 name##_CodeEnd __attribute__((section(".vutext")));                            \
    inline ps2::renderer::vu::MicroCode name##_Code()                                             \
    {                                                                                             \
        return { &name##_CodeStart, &name##_CodeEnd };                                            \
    }

// The programs in config/sources.mk's VCL_FILES.
PS2_DECLARE_VU_MICROPROGRAM(passthrough)

namespace ps2::renderer::dma
{
class DisplayList;
}

namespace ps2::renderer::vu
{

// Micro memory: 2048 64-bit instructions. Addresses are in that unit, as MPG and MSCAL take them.
static constexpr u32 kMicroMemInstructions = 2048U;

enum class Program : u8
{
    Passthrough,
    Count
};

enum class MicroAddr : u32
{
};

struct Stats
{
    u32 uploads;   // MPG chains emitted
    u32 evictions; // swap region resets that dropped loaded programs
};

// Measures every program, lays out the resident region and the swap region and prebuilds one
// MPG chain per program in main memory. After dma::Init; Shutdown frees the chains.
PS2_COLD_FUNC bool Init();
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

// Emits the program's MPG chain into 'list' (one CALL tag) only when it is not resident in micro
// memory, and returns the MSCAL entry address. Resident programs upload on first use after Init
// or after micro memory was invalidated; swappable ones take the swap region, evicting its
// contents when they do not fit. Draw lists are sorted by program so swaps stay rare.
MicroAddr Bind(dma::DisplayList & list, Program program);

// Micro memory contents are unknown again (after a VU reset or a foreign upload): every program
// uploads on its next Bind.
void Invalidate();

// Uploads the shared VU data memory constants a frame relies on (today the null GIF tag at
// kNullGifTagAddr): one CNT block, emitted once per frame before the first batch.
void EmitFrameConstants(dma::DisplayList & list);

// Qwords a Bind or EmitFrameConstants reserves at most, for dma::Ensure.
static constexpr u32 kBindQwords = 1U;
static constexpr u32 kFrameConstantQwords = 2U;

const Stats & GetStats();

} // namespace ps2::renderer::vu
