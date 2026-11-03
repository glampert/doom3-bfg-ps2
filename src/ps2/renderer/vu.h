// ================================================================================================
// File: vu.h
// Brief: The VU1 microprograms: their link symbols in the ELF's .vutext section and the VU1 data
//        memory map they share with vu1progs/vu_memory.i. The program manager (upload, the swap
//        region, Bind) joins this header with the display list.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

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
