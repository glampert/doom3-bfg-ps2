// ================================================================================================
// File: vu.h
// Brief: The VU1 microprograms: their link symbols in the ELF's .vutext section and the VU1 data
//        memory map they share through vu1progs/vu_memory.h, and the program manager (upload, the
//        swap region, Bind) the display list binds programs through.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/math/vec_mat.h"
#include "ps2/renderer/vu1progs/vu_memory.h"
#include <tamtypes.h>

namespace ps2::renderer::vu
{

// An assembled microprogram's extent in .vutext (dvp-as's `.vu` section; quake2-ps2
// calls it .vudata). Instruction counts are in 64-bit VU instructions, the unit MPG
// upload destinations and MSCAL entry points use, not qwords (half as many).
struct MicroCode
{
    const u32 * start;
    const u32 * end;

    u32 InstructionCount() const
    {
        return static_cast<u32>(end - start) / 2u;
    }
};

// Declares the linker symbols bracketing an assembled microprogram and its MicroCode accessor.
// 'name' must match the #vuprog name in the .vcl. Use it at global scope: an extern "C" inside
// an anonymous namespace gets internal linkage and does not resolve against the VU object.
#define PS2_DECLARE_VU_MICROPROGRAM(name)                                \
    extern "C" u32 name##_CodeStart __attribute__((section(".vutext"))); \
    extern "C" u32 name##_CodeEnd   __attribute__((section(".vutext"))); \
    inline ps2::renderer::vu::MicroCode name##_Code()                    \
    {                                                                    \
        return { &name##_CodeStart, &name##_CodeEnd };                   \
    }

// The programs in config/sources.mk's VCL_FILES.
PS2_DECLARE_VU_MICROPROGRAM(passthrough)
PS2_DECLARE_VU_MICROPROGRAM(world_lm)

// Micro memory: 2048 64-bit instructions. Addresses are in that unit, as MPG and MSCAL take them.
static constexpr u32 kMicroMemInstructions = 2048U;

enum class Program : u8
{
    Passthrough,
    WorldLightmapped, // world_lm.vcl: one cooked D3WD batch per MSCAL
    Count
};

// The clipper's parameters (vu_clip.i), which reach it through the frame constants. A triangle
// survives the guard band judgement out to |x/w| and |y/w| of 0.8, the reach of the 12.4 window
// coordinates (the visible screen ends near 0.13); the clipper cuts to that band.
static constexpr float kGuardBandScale = 1.0f / 0.8f;

// The clipper cuts this fraction of w inside each plane, so the survivors' own judgement agrees,
// and scales distances so the ftoi4 sign of a product of two survives (quake2-ps2's values).
static constexpr float kClipShrink = 0.001f;
static constexpr float kClipDistanceScale = 2048.0f * 2048.0f;

// What a frame's programs read from the low data memory (vu_memory.h): the view-projection (row
// vectors), the projection to GS window coordinates and the guard band, and the world programs'
// drawing tags. DisplayList::SetFrameConstants adds the window state and the null tag itself.
struct alignas(16) FrameConstants
{
    math::Mat4 viewProjection;
    math::Vec4 gsScale;   // w: 1 - kClipShrink
    math::Vec4 gsOffset;  // w: kClipDistanceScale
    math::Vec4 clipScale; // kGuardBandScale (xy), 1 for z
    qword_t stripTag;     // drawing tags (gs::DrawingTag); NLOOP and EOP are the program's
    qword_t listTag;
};

enum class MicroAddr : u32
{
};

// Measures every program, lays out the resident region and the swap region and prebuilds one
// MPG chain per program in main memory. After dma::Init; Shutdown frees the chains.
PS2_COLD_FUNC bool Init();
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

// Where a bound program starts, and the MPG chain (ending in RET) the caller must CALL before it,
// or null when the program is already in micro memory.
struct Binding
{
    MicroAddr entry;
    const qword_t * upload;
};

// Makes 'program' current in micro memory for DisplayList::SetProgram, which CALLs the upload.
// Resident programs upload on first use after Init or after micro memory was invalidated;
// swappable ones take the swap region, evicting its contents when they do not fit. Draw lists
// are sorted by program so swaps stay rare.
Binding Bind(Program program);

// Micro memory contents are unknown again (after a VU reset or a foreign upload): every program
// uploads on its next Bind.
void Invalidate();

struct Stats
{
    u32 uploads;   // MPG chains emitted
    u32 evictions; // swap region resets that dropped loaded programs
};

const Stats & GetStats();

} // namespace ps2::renderer::vu
