// ================================================================================================
// File: gs.cpp
// Brief: Owns GS contexts, direct RGBA and paletted uploads and ordered CPU primitives, and the
//        window transform and drawing tags VU1 programs need.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/gs.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/pipeline_wait.h"

#include <idlib/precompiled.h>
#include <dma.h>
#include <dma_tags.h>
#include <draw_primitives.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>

namespace ps2::renderer::gs
{
namespace
{

static framebuffer_t s_frames[2] = {};
static zbuffer_t s_depth = {};
static FrameFormat s_format = FrameFormat::Color16;
static u32 s_displayAddress = 0; // the 32/16 profile's PSMCT16S display buffer
static u32 s_presentedAddress = 0;
static system::video::Interlace s_interlace = system::video::Interlace::Field;
static int s_width = 0;
static int s_height = 0;
static u32 s_fixedVramBytes = 0;
static bool s_initialized = false;
static u64 s_previousImr = 0;
alignas(64) static qword_t s_upload[128] = {};
alignas(64) static qword_t s_copy[80] = {};

// z/w of +-1 times this, plus the same offset, then x16 (ftoi4) spans the Z16 range [0, 0xFFFF].
static constexpr float kDepthScale = static_cast<float>(0xFFFF) / 32.0f;

ID_FORCE_INLINE int Index(DrawContext context)
{
    const int index = static_cast<int>(context);
    PS2_AssertMsg(index >= 0 && index <= 1, "invalid GS drawing context");
    return index;
}

ID_FORCE_INLINE u64 ContextReg(int reg, DrawContext context)
{
    return static_cast<u64>(reg + Index(context));
}

ID_FORCE_INLINE u64 AlphaState(BlendMode blend)
{
    // GS computes (A - B) * C + D, where A, B and D select Cs (0), Cd (1) or zero (2) and C is As.
    // SWF adds its premultiplied source to Cd, so straight input selects zero for B.
    switch (blend)
    {
    case BlendMode::AdditiveAlpha:
        return GS_SET_ALPHA(0, 2, 0, 1, 0x80);
    case BlendMode::SubtractAlpha:
        return GS_SET_ALPHA(2, 0, 0, 1, 0x80);
    case BlendMode::Screen:
        return GS_SET_ALPHA(1, 2, 0, 1, 0x80);
    case BlendMode::Replace:
    case BlendMode::StraightAlpha:
        break;
    }

    return GS_SET_ALPHA(0, 1, 0, 1, 0x80);
}

ID_FORCE_INLINE bool Blended(BlendMode blend)
{
    return blend != BlendMode::Replace;
}

#if PS2_D3BFG_ASSERTS
ID_FORCE_INLINE void ValidateState(const DrawState & state)
{
    const auto blend = static_cast<u32>(state.blend);

    if (blend > static_cast<u32>(BlendMode::Screen) || state.channels == 0 || (state.channels & ~kWriteRgb) != 0) [[unlikely]]
    {
        FatalError("invalid GS draw state");
    }
}
#endif // PS2_D3BFG_ASSERTS

// The GS manual prohibits ZTE = 0. ALWAYS, with depth writes masked by the environment, reads and
// writes no depth; GEQUAL at depth 0 passes only where a mask wrote 0 (ClearFrame writes 1).
ID_FORCE_INLINE u64 TestState(bool masked)
{
    return GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, masked ? 2 : 1);
}

// FBMSK follows the 32-bit pixel in both profiles; PSMCT16 keeps R 7:3, G 15:11, B 23:19, A 31.
ID_FORCE_INLINE u64 FrameState(DrawContext context, u32 mask)
{
    const auto & frame = s_frames[Index(context)];
    return GS_SET_FRAME(frame.address >> 11, frame.width >> 6, frame.psm, mask);
}

ID_FORCE_INLINE u32 ChannelMask(u8 channels)
{
    return ((channels & kWriteRed)   != 0 ? 0U : 0x000000FFU) |
           ((channels & kWriteGreen) != 0 ? 0U : 0x0000FF00U) |
           ((channels & kWriteBlue)  != 0 ? 0U : 0x00FF0000U);
}

ID_FORCE_INLINE u64 DepthState(bool write)
{
    return GS_SET_ZBUF(s_depth.address >> 11, s_depth.zsm, write ? 0 : 1);
}

// The fog blend is (F * C + (255 - F) * FOGCOL) >> 8; at F = 0 one more than the color lands exactly.
ID_FORCE_INLINE u64 FogState(Color color)
{
    const auto Channel = [](u8 value) { return value == 255 ? 255U : static_cast<u32>(value) + 1U; };
    return GS_SET_FOGCOL(Channel(color.r), Channel(color.g), Channel(color.b));
}

// Pass state written before a primitive's vertices; EndPass restores the framebuffer mask.
ID_FORCE_INLINE void BeginPass(GifWriter & writer, DrawContext context, const DrawState & state)
{
    if (state.channels != kWriteRgb)
    {
        writer.SetRegister(ContextReg(GS_REG_FRAME, context), FrameState(context, ChannelMask(state.channels)));
    }

    if (state.constantColor)
    {
        writer.SetRegister(GS_REG_FOGCOL, FogState(state.color));
    }

    writer.SetRegister(ContextReg(GS_REG_ALPHA, context), AlphaState(state.blend));
    writer.SetRegister(ContextReg(GS_REG_TEST, context), TestState(state.masked));
}

ID_FORCE_INLINE void EndPass(GifWriter & writer, DrawContext context, const DrawState & state)
{
    if (state.channels != kWriteRgb)
    {
        writer.SetRegister(ContextReg(GS_REG_FRAME, context), FrameState(context, 0));
    }
}

// Vertex position; a constant-color pass carries fog coefficient 0 (full fog) in XYZF2.
ID_FORCE_INLINE void Position(GifWriter & writer, const DrawState & state, int x, int y)
{
    if (state.constantColor)
    {
        writer.SetRegister(GS_REG_XYZF2, GS_SET_XYZF(x + 2048 * 16, y + 2048 * 16, 0, 0));
    }
    else
    {
        writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(x + 2048 * 16, y + 2048 * 16, 0));
    }
}

// Mask writes touch only depth: every color/alpha bit masked, depth written, test always passing.
ID_FORCE_INLINE void BeginMaskWrite(GifWriter & writer, DrawContext context)
{
    writer.SetRegister(ContextReg(GS_REG_FRAME, context), FrameState(context, 0xFFFFFFFFU));
    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(true));
    writer.SetRegister(ContextReg(GS_REG_TEST, context), TestState(false));
}

ID_FORCE_INLINE void EndMaskWrite(GifWriter & writer, DrawContext context)
{
    writer.SetRegister(ContextReg(GS_REG_FRAME, context), FrameState(context, 0));
    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(false));
}

ID_FORCE_INLINE bool FixedPointInside(int x, int y)
{
    return x >= 0 && y >= 0 && x <= s_width * 16 && y <= s_height * 16;
}

#if PS2_D3BFG_ASSERTS
// The texels alone: an upload writes them and reads no CLUT or sampling state.
tex::Layout ValidateStorage(const TextureBinding & binding)
{
    tex::Layout verified = {};
    const auto & input = binding.layout;
    const bool made = input.format != tex::PixelFormat::Rgba32 ? tex::MakeIndexedLayout(input.width, input.height, input.format, verified)
                                                               : tex::MakeLayout(input.width, input.height, binding.sampler, verified);

    if (!s_initialized || !made || verified.bytes != input.bytes || verified.stridePixels != input.stridePixels ||
        verified.storageHeight != input.storageHeight || verified.potWidth != input.potWidth ||
        verified.potHeight != input.potHeight || verified.widthLog2 != input.widthLog2 ||
        verified.heightLog2 != input.heightLog2 || (binding.address & 2047U) != 0 ||
        binding.address < s_fixedVramBytes / 4U || binding.address > (4U * 1024U * 1024U - verified.bytes) / 4U) [[unlikely]]
    {
        FatalError("invalid GS texture storage");
    }

    return verified;
}

tex::Layout ValidateBinding(const TextureBinding & binding)
{
    const auto verified = ValidateStorage(binding);
    const bool indexed = verified.format != tex::PixelFormat::Rgba32;
    static constexpr u32 kVramWords = 1024U * 1024U;
    // A CLUT is a whole 256-byte block of texture VRAM: 16x16 or 8x2 PSMCT32 texels at CBP.
    const bool clut = !indexed ? binding.clutAddress == 0
                               : (binding.clutAddress & 63U) == 0 && binding.clutAddress >= s_fixedVramBytes / 4U &&
                                     binding.clutAddress <= kVramWords - (verified.format == tex::PixelFormat::Index8 ? 256U : 64U);
    const bool region = !binding.region || (binding.regionMinU <= binding.regionMaxU && binding.regionMaxU < verified.width &&
                                            binding.regionMinV <= binding.regionMaxV && binding.regionMaxV < verified.height);

    if (!clut || !region) [[unlikely]]
    {
        FatalError("invalid GS texture binding");
    }

    return verified;
}
#endif // PS2_D3BFG_ASSERTS

ID_FORCE_INLINE int StoragePsm(const tex::Layout & layout)
{
    return layout.format == tex::PixelFormat::Index8 ? GS_PSM_8 : layout.format == tex::PixelFormat::Index4 ? GS_PSM_4 : GS_PSM_32;
}

u64 TextureState(const TextureBinding & binding)
{
    const auto & layout = binding.layout;

    if (layout.format == tex::PixelFormat::Rgba32)
    {
        return GS_SET_TEX0(binding.address >> 6, static_cast<u32>(layout.stridePixels) >> 6, GS_PSM_32, layout.widthLog2,
                           layout.heightLog2, 1, 0, 0, 0, 0, 0, 0);
    }

    // CSM1 PSMCT32 CLUT, reloaded with every TEX0: CLUT texels can move or change with residency.
    return GS_SET_TEX0(binding.address >> 6, static_cast<u32>(layout.stridePixels) >> 6, StoragePsm(layout), layout.widthLog2,
                       layout.heightLog2, 1, 0, binding.clutAddress >> 6, GS_PSM_32, 0, 0, 1);
}

u64 ClampState(const TextureBinding & binding)
{
    if (binding.region)
    {
        return GS_SET_CLAMP(2, 2, binding.regionMinU, binding.regionMaxU, binding.regionMinV, binding.regionMaxV);
    }

    const auto & layout = binding.layout;
    const auto & sampler = binding.sampler;
    return GS_SET_CLAMP(sampler.wrapS == tex::Wrap::Clamp ? 2 : 0, sampler.wrapT == tex::Wrap::Clamp ? 2 : 0, 0, layout.width - 1, 0,
                        layout.height - 1);
}

// The draw buffers' format.
ID_FORCE_INLINE int ColorPsm(FrameFormat format)
{
    return format == FrameFormat::Color16 ? GS_PSM_16 : GS_PSM_32;
}

ID_FORCE_INLINE int DepthPsm(FrameFormat format)
{
    return format == FrameFormat::Color16 ? GS_ZBUF_16 : GS_ZBUF_16S;
}

// The format the display circuits read: PSMCT16S for the 32/16 copy, of the depth buffer's group.
ID_FORCE_INLINE int DisplayPsm(FrameFormat format)
{
    return format == FrameFormat::Color32Display16 ? GS_PSM_16S : ColorPsm(format);
}

ID_FORCE_INLINE bool SplitDisplay(FrameFormat format)
{
    return format == FrameFormat::Color32Display16;
}

// A buffer's height rounded up to whole GS page rows: 16-bit formats page 64x64 texels, 32-bit
// ones 64x32. The GS addresses a buffer by whole page rows, so a 224-line PSMCT16 buffer still
// reaches into its fourth row; another buffer starting inside it would overlap.
ID_FORCE_INLINE int PaddedRows(int psm, int height)
{
    const int pageRows = psm == GS_PSM_32 ? 32 : 64;
    return (height + pageRows - 1) / pageRows * pageRows;
}

// Both colour buffers (or the 32/16 profile's draw buffer and its display buffer), then the depth
// buffer, page-aligned from VRAM address 0. In 32/16 both drawing contexts name the one draw buffer.
PS2_COLD_FUNC bool LayOutBuffers(FrameFormat format, int width, int height)
{
    const int colorRows = PaddedRows(ColorPsm(format), height);
    const int secondRows = PaddedRows(DisplayPsm(format), height);
    const int depthRows = PaddedRows(DepthPsm(format), height);

    graph_vram_clear();
    const int first  = graph_vram_allocate(width, colorRows, ColorPsm(format), GRAPH_ALIGN_PAGE);
    const int second = graph_vram_allocate(width, secondRows, DisplayPsm(format), GRAPH_ALIGN_PAGE);
    const int depth  = graph_vram_allocate(width, depthRows, DepthPsm(format), GRAPH_ALIGN_PAGE);

    if (first < 0 || second < 0 || depth < 0)
    {
        graph_vram_clear();
        return false;
    }

    for (int index = 0; index < 2; ++index)
    {
        s_frames[index].width   = static_cast<unsigned int>(width);
        s_frames[index].height  = static_cast<unsigned int>(height);
        s_frames[index].address = static_cast<unsigned int>(index == 0 || SplitDisplay(format) ? first : second);
        s_frames[index].psm     = static_cast<unsigned int>(ColorPsm(format));
        s_frames[index].mask    = 0;
    }

    s_depth.address = static_cast<unsigned int>(depth);
    s_depth.zsm     = static_cast<unsigned int>(DepthPsm(format));
    s_depth.mask    = 1;
    s_depth.enable  = DRAW_DISABLE;
    s_depth.method  = ZTEST_METHOD_ALLPASS;
    s_format        = format;
    s_displayAddress = SplitDisplay(format) ? static_cast<u32>(second) : 0;
    s_fixedVramBytes = static_cast<u32>(depth + graph_vram_size(width, depthRows, DepthPsm(format), GRAPH_ALIGN_PAGE)) * 4U;
    return true;
}

// The read circuits show 'address'. FIELD mode blends the two circuits a line apart (libgraph's
// flicker filter); FRAME mode reads the whole buffer on every field through circuit 2 alone, so
// both circuits name line 0 and the one-line offset would only drop the first line.
void SetDisplayBuffer(u32 address)
{
    s_presentedAddress = address;

    if (s_interlace == system::video::Interlace::Field)
    {
        graph_set_framebuffer_filtered(static_cast<int>(address), s_width, DisplayPsm(s_format), 0, 0);
        return;
    }

    graph_set_framebuffer(0, static_cast<int>(address), s_width, DisplayPsm(s_format), 0, 0);
    graph_set_framebuffer(1, static_cast<int>(address), s_width, DisplayPsm(s_format), 0, 0);
}

// The ordered dither the copy adds before PSMCT16S drops each channel's low three bits, 3-bit
// signed entries a row at a time (libdraw's matrix). GS_SET_DIMX masks entries with 0x3, so pack
// them by hand.
u64 DitherMatrix()
{
    static constexpr int kMatrix[16] = { -4, 2, -3, 3, 0, -2, 1, -1, -3, 3, -4, 2, 1, -1, 0, -2 };
    u64 packed = 0;

    for (int index = 0; index < 16; ++index)
    {
        packed |= static_cast<u64>(static_cast<u32>(kMatrix[index]) & 0x7U) << (index * 4);
    }

    return packed;
}

// The 32/16 present: textured sprites copy the PSMCT32 draw buffer texel for texel into the
// PSMCT16S display buffer with dithering, in 64-pixel columns that keep the texture cache on one
// page column. Context 0's frame, test and texture state are rewritten and the frame restored;
// every primitive writes its own texture state, so nothing else is inherited.
void CopyToDisplay(u32 drawAddress)
{
    PS2_AssertMsg(SplitDisplay(s_format), "display copy outside the 32/16 profile");

    qword_t * q = s_copy + 1;
    const auto Set = [&q](u64 reg, u64 data)
    {
        q->dw[0] = data;
        q->dw[1] = reg;
        ++q;
    };

    // The UVs sit half a texel in, so each pixel center samples its own texel.
    Set(GS_REG_TEXFLUSH, 0);
    Set(GS_REG_FRAME_1, GS_SET_FRAME(s_displayAddress >> 11, static_cast<u32>(s_width) >> 6, GS_PSM_16S, 0));
    Set(GS_REG_ZBUF_1, DepthState(false));
    Set(GS_REG_TEST_1, TestState(false));
    Set(GS_REG_TEX0_1, GS_SET_TEX0(drawAddress >> 6, static_cast<u32>(s_width) >> 6, GS_PSM_32, 10, 9, 0, 1, 0, 0, 0, 0, 0));
    Set(GS_REG_TEX1_1, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0));
    Set(GS_REG_CLAMP_1, GS_SET_CLAMP(1, 1, 0, 0, 0, 0));
    Set(GS_REG_DIMX, DitherMatrix());
    Set(GS_REG_DTHE, GS_SET_DTHE(1));
    Set(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 1, 0, 0, 0, 1, 0, 0));
    Set(GS_REG_RGBAQ, GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x3F800000U));

    for (int x = 0; x < s_width; x += 64)
    {
        Set(GS_REG_UV, GS_SET_UV(x * 16 + 8, 8));
        Set(GS_REG_XYZ2, GS_SET_XYZ((2048 + x) * 16, 2048 * 16, 0));
        Set(GS_REG_UV, GS_SET_UV((x + 64) * 16 + 8, s_height * 16 + 8));
        Set(GS_REG_XYZ2, GS_SET_XYZ((2048 + x + 64) * 16, (2048 + s_height) * 16, 0));
    }

    Set(GS_REG_DTHE, GS_SET_DTHE(0));
    Set(GS_REG_FRAME_1, FrameState(DrawContext::Ctx0, 0));
    Set(GS_REG_FINISH, 1);

    const u32 count = static_cast<u32>(q - s_copy);
    PS2_AssertMsg(count <= static_cast<u32>(ArrayLength(s_copy)), "display copy packet overflow");
    PACK_GIFTAG(s_copy, GIF_SET_TAG(count - 1U, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);

    FlushCache(0);
    *GS_REG_CSR = 2;
    dma_channel_send_normal(DMA_CHANNEL_GIF, s_copy, static_cast<int>(count), 0, 0);
    WaitUntil([] { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "display copy GIF DMA");
    WaitUntil([] { return (*GS_REG_CSR & 2U) != 0; }, "display copy GS FINISH");
}

} // namespace

PS2_COLD_FUNC bool Init(const Config & config)
{
    if (s_initialized || !dma::IsInitialized()) [[unlikely]]
    {
        FatalError("invalid GS initialization order");
    }

    // One of the standard's resolutions (video_mode.cpp) at the lines its interlace mode draws.
    const bool known = system::video::IsValid(config.standard) && system::video::IsValid(config.interlace) &&
                       (config.format == FrameFormat::Color16 || config.format == FrameFormat::Color32 ||
                        config.format == FrameFormat::Color32Display16);
    const int lines = known ? system::video::VisibleLines(config.standard) : 0;
    const int index = known ? system::video::FindResolution(config.standard, { config.width, lines }) : -1;

    if (index < 0 || config.height != system::video::FrameHeight(system::video::ResolutionAt(config.standard, index), config.interlace))
    {
        Log(LogLevel::Error, "unsupported GS device configuration\n");
        return false;
    }

    if (!LayOutBuffers(config.format, config.width, config.height))
    {
        return false;
    }

    s_interlace = config.interlace;
    s_width = config.width;
    s_height = config.height;
    s_previousImr = GsGetIMR();

    // FIELD mode blends the two read circuits a line apart (the flicker filter); FRAME mode shows
    // the same lines on both fields, where the filter would only blur adjacent buffer lines.
    const bool field = config.interlace == system::video::Interlace::Field;

    if (graph_set_mode(GRAPH_MODE_INTERLACED, system::video::GraphMode(config.standard), field ? GRAPH_MODE_FIELD : GRAPH_MODE_FRAME,
                       field ? GRAPH_ENABLE : GRAPH_DISABLE) != 0)
    {
        graph_shutdown();
        GsPutIMR(s_previousImr);
        graph_vram_clear();
        s_format = FrameFormat::Color16;
        s_displayAddress = 0;
        s_interlace = system::video::Interlace::Field;
        s_width = s_height = 0;
        s_fixedVramBytes = 0;
        return false;
    }

    graph_disable_output();

    // Claim the VIF/GIF interfaces as well as the GS. A GS CSR reset alone left
    // inherited loader state that blocked our first PATH2 DIRECT on the target.
    WaitUntil([] { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "initial GIF DMA");
    Log(LogLevel::Info, "[D3BFG] GS claim GIF_STAT=%08x VIF1_STAT=%08x\n", *R_EE_GIF_STAT, *R_EE_VIF1_STAT);

    *R_EE_GIF_CTRL = 1;
    *R_EE_GIF_CTRL = 0;
    *R_EE_VIF1_FBRST = 1;
    *R_EE_VIF1_FBRST = 0;
    EE_SYNCP();

    // Poll CSR directly; no interrupt handler may consume our FINISH/VSYNC events.
    GsPutIMR(0x7F00);
    graph_set_screen(0, 0, s_width, s_height);
    graph_set_bgcolor(0, 0, 0);
    SetDisplayBuffer(SplitDisplay(s_format) ? s_displayAddress : s_frames[0].address);
    s_initialized = true;

    if (dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0) < 0)
    {
        Shutdown();
        return false;
    }

    dma::BeginFrame();
    {
        dma::DisplayList & list = dma::List();
        GifWriter writer = list.BeginDirect(32);
        writer.SetupEnvironment(0, s_frames[0], s_depth);
        writer.SetupEnvironment(1, s_frames[1], s_depth);
        list.EndDirect(writer);
        ClearFrame(list, DrawContext::Ctx0, { 0, 0, 0, 255 });
    }
    dma::EndFrame();

    Log(LogLevel::Info, "[D3BFG] GS ready %s %dx%d %s %s fixed_vram=%u\n", system::video::Name(config.standard), s_width, s_height,
        system::video::InterlaceName(s_interlace), FrameFormatName(s_format), s_fixedVramBytes);

    return true;
}

FrameFormat GetFrameFormat()
{
    return s_format;
}

const char * FrameFormatName(FrameFormat format)
{
    return format == FrameFormat::Color32 ? "PSMCT32/Z16S" : format == FrameFormat::Color32Display16 ? "PSMCT32+PSMCT16S/Z16S" : "PSMCT16/Z16";
}

const char * FrameFormatLabel(FrameFormat format)
{
    return format == FrameFormat::Color32 ? "32/32" : format == FrameFormat::Color32Display16 ? "32/16" : "16/16";
}

int FrameFormatBits(FrameFormat format)
{
    return format == FrameFormat::Color16 ? 16 : 32;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    dma::WaitIdle();
    dma_channel_shutdown(DMA_CHANNEL_GIF, 0);
    graph_shutdown();
    GsPutIMR(s_previousImr);
    graph_vram_clear();

    s_frames[0] = {};
    s_frames[1] = {};
    s_depth = {};
    s_format = FrameFormat::Color16;
    s_displayAddress = 0;
    s_presentedAddress = 0;
    s_interlace = system::video::Interlace::Field;
    s_width = s_height = 0;
    s_fixedVramBytes = 0;
    s_initialized = false;
}

bool IsInitialized()
{
    return s_initialized;
}

int Width()
{
    return s_width;
}

int Height()
{
    return s_height;
}

system::video::Interlace GetInterlace()
{
    return s_interlace;
}

u32 FixedVramBytes()
{
    return s_fixedVramBytes;
}

FramebufferInfo GetFramebuffer(DrawContext context)
{
    PS2_AssertMsg(s_initialized, "framebuffer query before GS initialization");
    return { s_frames[Index(context)].address, s_width, s_height, static_cast<int>(s_frames[Index(context)].psm) };
}

FramebufferInfo GetDisplayFramebuffer()
{
    PS2_AssertMsg(s_initialized, "framebuffer query before GS initialization");
    return { s_presentedAddress, s_width, s_height, DisplayPsm(s_format) };
}

void Present(DrawContext context, PresentMode mode)
{
    PS2_AssertMsg(s_initialized, "GS present before initialization");

    if (mode == PresentMode::Immediate)
    {
        dma::WaitIdle();
    }
    else
    {
        PS2_AssertMsg(!dma::InFlight(), "pipelined present while a chain is still queued");
    }

    *GS_REG_CSR = 8; // Fresh field boundary, without clearing FINISH or other events.
    WaitUntil([] { return (*GS_REG_CSR & 8U) != 0; }, "VSYNC");

    // CSR.FIELD (bit 13) is the field now being displayed. In FIELD mode a flip at the start of
    // the EVEN field shows the whole frame for two fields; one that lands on ODD waits for the
    // next vsync. FRAME mode shows the whole buffer on either field.
    const bool pairFields = mode == PresentMode::Pipelined && s_interlace == system::video::Interlace::Field;

    while (pairFields && (*GS_REG_CSR & (1U << 13)) != 0U)
    {
        *GS_REG_CSR = 8;
        WaitUntil([] { return (*GS_REG_CSR & 8U) != 0; }, "VSYNC (even field)");
    }

    if (SplitDisplay(s_format))
    {
        CopyToDisplay(s_frames[Index(context)].address);
        SetDisplayBuffer(s_displayAddress);
    }
    else
    {
        SetDisplayBuffer(s_frames[Index(context)].address);
    }

    graph_enable_output();
}

math::Vec4 WindowScale()
{
    return { 2048.0f, 2048.0f, kDepthScale, 0.0f };
}

math::Vec4 WindowOffset()
{
    return { 2048.0f + static_cast<float>(s_width) * 0.5f, 2048.0f + static_cast<float>(s_height) * 0.5f, kDepthScale, 0.0f };
}

qword_t DrawingTag(Primitive primitive, VertexFormat format, DrawContext context)
{
    // In Primitive and VertexFormat order: the PRIM type, then the REGS nibbles and NREG.
    static constexpr u64 kTypes[] = {
        PRIM_POINT,
        PRIM_LINE, PRIM_LINE_STRIP,
        PRIM_TRIANGLE, PRIM_TRIANGLE_STRIP, PRIM_TRIANGLE_FAN,
        PRIM_SPRITE
    };
    static constexpr u64 kRegisters[][2] = { { static_cast<u64>(GIF_REG_RGBAQ) | (static_cast<u64>(GIF_REG_XYZ2) << 4), 2 } };

    static_assert(ArrayLength(kTypes) == static_cast<int>(Primitive::Sprite) + 1, "kTypes must follow Primitive");
    static_assert(ArrayLength(kRegisters) == static_cast<int>(VertexFormat::ColorPosition) + 1, "kRegisters must follow VertexFormat");
    PS2_AssertMsg(static_cast<int>(primitive) < ArrayLength(kTypes) && static_cast<int>(format) < ArrayLength(kRegisters),
                  "invalid GS drawing tag");

    const u64 * const registers = kRegisters[static_cast<int>(format)];
    const u64 prim = GS_SET_PRIM(kTypes[static_cast<int>(primitive)], PRIM_SHADE_GOURAUD, 0, 0, 0, 0, 0, static_cast<u64>(Index(context)), 0);

    qword_t tag;
    tag.dw[0] = GIF_SET_TAG(0, 0, 1, prim, GIF_FLG_PACKED, registers[1]);
    tag.dw[1] = registers[0];
    return tag;
}

void Upload(const TextureBinding & binding, const void * pixels)
{
    const auto & layout = binding.layout;

    // Audited draw.c: six setup qwords and three per IMAGE/REF chunk. The largest 512x512 layout
    // MakeLayout admits fits s_upload; assertion builds check the chain before and after the SDK.
#if PS2_D3BFG_ASSERTS
    (void)ValidateStorage(binding);
    PS2_AssertMsg(pixels != nullptr && (reinterpret_cast<uiptr>(pixels) & 63U) == 0, "invalid GS texture upload");

    const u32 chunks = (layout.bytes / 16U + GIF_BLOCK_SIZE - 1U) / GIF_BLOCK_SIZE;
    const u32 transferQwords = 6U + 3U * chunks;
    PS2_AssertMsg(transferQwords + 4U <= static_cast<u32>(ArrayLength(s_upload)), "texture upload chain overflow");
#endif // PS2_D3BFG_ASSERTS

    // Retire queued PATH2 references before PATH3 uploads or overwrites an address.
    dma::Kick();
    dma::WaitIdle();
    WaitUntil([] { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "texture GIF DMA idle");

    qword_t * end = draw_texture_transfer(s_upload, const_cast<void *>(pixels), layout.stridePixels,
                                          layout.storageHeight, StoragePsm(layout), static_cast<int>(binding.address),
                                          layout.stridePixels);

#if PS2_D3BFG_ASSERTS
    PS2_AssertMsg(end == s_upload + transferQwords, "unexpected SDK texture packet size");
#endif // PS2_D3BFG_ASSERTS

    DMATAG_END(end, 3, 0, 0, 0);
    ++end;
    PACK_GIFTAG(end, GIF_SET_TAG(2, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    ++end;
    PACK_GIFTAG(end, 0, GS_REG_TEXFLUSH);
    ++end;
    PACK_GIFTAG(end, 1, GS_REG_FINISH);

    // Both the chain and REF pixels require writeback before the synchronous upload.
    FlushCache(0);
    *GS_REG_CSR = 2;

    dma_channel_send_chain(DMA_CHANNEL_GIF, s_upload, 0, 0, 0);
    WaitUntil([] { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "texture GIF DMA");
    WaitUntil([] { return (*GS_REG_CSR & 2U) != 0; }, "texture GS FINISH");
}

void TexturedRect(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                  int x, int y, int width, int height,
                  int u0, int v0, int u1, int v1, Color color)
{
#if PS2_D3BFG_ASSERTS
    if (!s_initialized || width < 0 || height < 0 || u0 < 0 || v0 < 0 || u1 < u0 || v1 < v0 ||
        u1 > 1023 || v1 > 1023) [[unlikely]]
    {
        FatalError("invalid GS textured rectangle");
    }

    (void)Index(context);
    (void)ValidateBinding(binding);
#endif // PS2_D3BFG_ASSERTS

    const s64 right = static_cast<s64>(x) + width;
    const s64 bottom = static_cast<s64>(y) + height;
    const int left = x > 0 ? x : 0;
    const int top = y > 0 ? y : 0;
    const int endX = static_cast<int>(right < s_width ? right : s_width);
    const int endY = static_cast<int>(bottom < s_height ? bottom : s_height);
    if (left >= endX || top >= endY || color.a == 0)
    {
        return;
    }

    // Interpolate UV in wide fixed point after screen clipping; no wrapped coordinates.
    const auto Coord = [](int start, int end, s64 offset, int extent)
    {
        return static_cast<int>(static_cast<s64>(start) * 16 + static_cast<s64>(end - start) * 16 * offset / extent);
    };

    const int startU = Coord(u0, u1, static_cast<s64>(left) - x, width);
    const int startV = Coord(v0, v1, static_cast<s64>(top)  - y, height);
    const int endU   = Coord(u0, u1, static_cast<s64>(endX) - x, width);
    const int endV   = Coord(v0, v1, static_cast<s64>(endY) - y, height);

    TexturedRectFixed(list, context, binding, left * 16, top * 16, endX * 16, endY * 16,
                      startU, startV, endU, endV, color, DrawState{});
}

void TexturedRectFixed(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                       int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, Color color, const DrawState & state)
{
#if PS2_D3BFG_ASSERTS
    (void)ValidateBinding(binding);
    (void)Index(context);
    ValidateState(state);

    if (x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0 || x1 > s_width * 16 || y1 > s_height * 16 ||
        u0 < 0 || v0 < 0 || u1 < u0 || v1 < v0 || u1 > 16383 || v1 > 16383) [[unlikely]]
    {
        FatalError("invalid fixed GS textured rectangle");
    }
#endif // PS2_D3BFG_ASSERTS

    if (x0 == x1 || y0 == y1 || (color.a == 0 && Blended(state.blend)))
    {
        return;
    }

    const int filter = binding.sampler.filter == tex::Filter::Linear ? 1 : 0;

    GifWriter writer = list.BeginDirect(30);
    BeginPass(writer, context, state);

    writer.SetRegister(ContextReg(GS_REG_TEX1, context), GS_SET_TEX1(1, 0, filter, filter, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_TEX0, context), TextureState(binding));
    writer.SetRegister(ContextReg(GS_REG_CLAMP, context), ClampState(binding));
    // A zero-alpha texel still replaces RGB in ONE/ZERO mode. Environment cutout is
    // not the native GUI contract; the pass state disables it for every UI sprite.
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 1, state.constantColor ? 1 : 0,
                                                Blended(state.blend) ? 1 : 0, 0, 1, Index(context), 0));
    const auto Modulate = [](u8 value) { return static_cast<u8>((static_cast<u32>(value) * 128U + 127U) / 255U); };
    writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(Modulate(color.r), Modulate(color.g), Modulate(color.b),
                                                  Modulate(color.a), 0x3F800000U));
    writer.SetRegister(GS_REG_UV, GS_SET_UV(u0, v0));
    Position(writer, state, x0, y0);
    writer.SetRegister(GS_REG_UV, GS_SET_UV(u1, v1));
    Position(writer, state, x1, y1);

    EndPass(writer, context, state);
    list.EndDirect(writer);
}

void TexturedTriangleFixed(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                           const ui::FixedTriangle & triangle, const DrawState & state)
{
#if PS2_D3BFG_ASSERTS
    (void)ValidateBinding(binding);
    (void)Index(context);
    ValidateState(state);

    for (const auto & vertex : triangle.vertices)
    {
        if (!FixedPointInside(vertex.x, vertex.y) || vertex.u < 0 || vertex.v < 0 || vertex.u > 16383 || vertex.v > 16383) [[unlikely]]
        {
            FatalError("invalid fixed GS textured triangle vertex");
        }
    }
#endif // PS2_D3BFG_ASSERTS

    const auto & first = triangle.vertices[0];
    const auto & second = triangle.vertices[1];
    const auto & third = triangle.vertices[2];
    const s64 area = static_cast<s64>(second.x - first.x) * (third.y - first.y) -
                     static_cast<s64>(second.y - first.y) * (third.x - first.x);
    if (area == 0 || (Blended(state.blend) && first.a == 0 && second.a == 0 && third.a == 0))
    {
        return;
    }

    const int filter = binding.sampler.filter == tex::Filter::Linear ? 1 : 0;

    // Complete state and three RGBAQ/UV/XYZ2 vertices: at most eighteen registers, thirty-six qwords.
    // A drain may alternate command halves; never depend on the previous primitive's state.
    GifWriter writer = list.BeginDirect(36);
    BeginPass(writer, context, state);

    writer.SetRegister(ContextReg(GS_REG_TEX1, context), GS_SET_TEX1(1, 0, filter, filter, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_TEX0, context), TextureState(binding));
    writer.SetRegister(ContextReg(GS_REG_CLAMP, context), ClampState(binding));

    // ONE/ZERO replaces RGB even where interpolated or texture alpha is zero.
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_TRIANGLE, PRIM_SHADE_GOURAUD, 1, state.constantColor ? 1 : 0,
                                                Blended(state.blend) ? 1 : 0, 0, PRIM_MAP_UV, Index(context), 0));
    const auto Modulate = [](u8 value) { return static_cast<u8>((static_cast<u32>(value) * 128U + 127U) / 255U); };
    for (const auto & vertex : triangle.vertices)
    {
        writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(Modulate(vertex.r), Modulate(vertex.g), Modulate(vertex.b),
                                                      Modulate(vertex.a), 0x3F800000U));
        writer.SetRegister(GS_REG_UV, GS_SET_UV(vertex.u, vertex.v));
        Position(writer, state, vertex.x, vertex.y);
    }

    EndPass(writer, context, state);
    list.EndDirect(writer);
}

void LineFixed(dma::DisplayList & list, DrawContext context, const ui::FixedLine & line, const DrawState & state)
{
    const auto & first  = line.vertices[0];
    const auto & second = line.vertices[1];

#if PS2_D3BFG_ASSERTS
    (void)Index(context);
    ValidateState(state);

    if (!s_initialized || !FixedPointInside(first.x, first.y) || !FixedPointInside(second.x, second.y)) [[unlikely]]
    {
        FatalError("invalid fixed GS line");
    }
#endif // PS2_D3BFG_ASSERTS

    if ((first.x == second.x && first.y == second.y) || (Blended(state.blend) && first.a == 0 && second.a == 0))
    {
        return;
    }

    // Untextured RGB is the pixel color itself; only alpha takes the 0x80 = 1.0 scale.
    GifWriter writer = list.BeginDirect(24);
    BeginPass(writer, context, state);

    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_LINE, PRIM_SHADE_GOURAUD, 0, state.constantColor ? 1 : 0,
                                                Blended(state.blend) ? 1 : 0, 0, 1, Index(context), 0));
    for (const auto & vertex : line.vertices)
    {
        const u8 alpha = static_cast<u8>((static_cast<u32>(vertex.a) * 128U + 127U) / 255U);
        writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(vertex.r, vertex.g, vertex.b, alpha, 0x3F800000U));
        Position(writer, state, vertex.x, vertex.y);
    }

    EndPass(writer, context, state);
    list.EndDirect(writer);
}

void FillRect(dma::DisplayList & list, DrawContext context, int x, int y, int width, int height, Color color)
{
#if PS2_D3BFG_ASSERTS
    if (!s_initialized || width < 0 || height < 0) [[unlikely]]
    {
        FatalError("invalid GS fill rectangle");
    }

    (void)Index(context);
#endif // PS2_D3BFG_ASSERTS

    // Wide arithmetic before clipping: untrusted dimensions must not wrap GS coordinates.
    const s64 right = static_cast<s64>(x) + width;
    const s64 bottom = static_cast<s64>(y) + height;
    const int left = x > 0 ? x : 0;
    const int top = y > 0 ? y : 0;
    const int endX = static_cast<int>(right < s_width ? right : s_width);
    const int endY = static_cast<int>(bottom < s_height ? bottom : s_height);
    if (left >= endX || top >= endY || color.a == 0)
    {
        return;
    }

    GifWriter writer = list.BeginDirect(12);

    writer.SetRegister(ContextReg(GS_REG_ALPHA, context), GS_SET_ALPHA(0, 1, 0, 1, 0x80));
    writer.SetRegister(ContextReg(GS_REG_TEST, context), TestState(false));

    const int blend = color.a == 255 ? 0 : 1;
    const u8 alpha = static_cast<u8>((static_cast<u32>(color.a) * 128U + 127U) / 255U);

    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, blend, 0, 1, Index(context), 0));
    writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(color.r, color.g, color.b, alpha, 0x3F800000U));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((left + 2048) * 16, (top + 2048) * 16, 0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((endX + 2048) * 16, (endY + 2048) * 16, 0));

    list.EndDirect(writer);
}

void ClearFrame(dma::DisplayList & list, DrawContext context, Color color)
{
#if PS2_D3BFG_ASSERTS
    if (!s_initialized || color.a != 255) [[unlikely]]
    {
        FatalError("invalid GS frame clear");
    }

    (void)Index(context);
#endif // PS2_D3BFG_ASSERTS

    // One sprite writes the color and depth 1, outside every mask; then depth writes stop again.
    GifWriter writer = list.BeginDirect(16);

    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(true));
    writer.SetRegister(ContextReg(GS_REG_TEST, context), TestState(false));
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 1, Index(context), 0));
    writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(color.r, color.g, color.b, 0x80, 0x3F800000U));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(2048 * 16, 2048 * 16, 1));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((2048 + s_width) * 16, (2048 + s_height) * 16, 1));
    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(false));

    list.EndDirect(writer);
}

void BeginDepthPass(dma::DisplayList & list, DrawContext context)
{
    PS2_AssertMsg(s_initialized, "depth pass before GS initialization");

    list.FlushPaths();

    // ZTST 2 is GEQUAL; ClearFrame left depth 1, the far end of the range. Each register is a
    // tag and its A+D qword.
    GifWriter writer = list.BeginDirect(4);
    writer.SetRegister(ContextReg(GS_REG_TEST, context), GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 2));
    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(true));
    list.EndDirect(writer);
}

void EndDepthPass(dma::DisplayList & list, DrawContext context)
{
    PS2_AssertMsg(s_initialized, "depth pass before GS initialization");

    list.FlushPaths();

    GifWriter writer = list.BeginDirect(2);
    writer.SetRegister(ContextReg(GS_REG_ZBUF, context), DepthState(false));
    list.EndDirect(writer);
}

void MaskTriangleFixed(dma::DisplayList & list, DrawContext context, const ui::FixedTriangle & triangle)
{
#if PS2_D3BFG_ASSERTS
    (void)Index(context);

    for (const auto & vertex : triangle.vertices)
    {
        if (!s_initialized || !FixedPointInside(vertex.x, vertex.y)) [[unlikely]]
        {
            FatalError("invalid fixed GS mask triangle");
        }
    }
#endif // PS2_D3BFG_ASSERTS

    const auto & first  = triangle.vertices[0];
    const auto & second = triangle.vertices[1];
    const auto & third  = triangle.vertices[2];
    const s64 area = static_cast<s64>(second.x - first.x) * (third.y - first.y) -
                     static_cast<s64>(second.y - first.y) * (third.x - first.x);

    if (area == 0)
    {
        return;
    }

    GifWriter writer = list.BeginDirect(20);
    BeginMaskWrite(writer, context);

    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_TRIANGLE, 0, 0, 0, 0, 0, 1, Index(context), 0));
    for (const auto & vertex : triangle.vertices)
    {
        writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(vertex.x + 2048 * 16, vertex.y + 2048 * 16, 0));
    }

    EndMaskWrite(writer, context);
    list.EndDirect(writer);
}

void MaskLineFixed(dma::DisplayList & list, DrawContext context, const ui::FixedLine & line)
{
    const auto & first  = line.vertices[0];
    const auto & second = line.vertices[1];

#if PS2_D3BFG_ASSERTS
    (void)Index(context);

    if (!s_initialized || !FixedPointInside(first.x, first.y) || !FixedPointInside(second.x, second.y)) [[unlikely]]
    {
        FatalError("invalid fixed GS mask line");
    }
#endif // PS2_D3BFG_ASSERTS

    if (first.x == second.x && first.y == second.y)
    {
        return;
    }

    GifWriter writer = list.BeginDirect(18);
    BeginMaskWrite(writer, context);

    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_LINE, 0, 0, 0, 0, 0, 1, Index(context), 0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(first.x  + 2048 * 16, first.y  + 2048 * 16, 0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(second.x + 2048 * 16, second.y + 2048 * 16, 0));

    EndMaskWrite(writer, context);
    list.EndDirect(writer);
}

void ClearMask(dma::DisplayList & list, DrawContext context, int x0, int y0, int x1, int y1)
{
#if PS2_D3BFG_ASSERTS
    (void)Index(context);

    if (!s_initialized || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0 || x1 > s_width || y1 > s_height) [[unlikely]]
    {
        FatalError("invalid GS mask clear");
    }
#endif // PS2_D3BFG_ASSERTS

    if (x0 == x1 || y0 == y1)
    {
        return;
    }

    GifWriter writer = list.BeginDirect(18);
    BeginMaskWrite(writer, context);

    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 1, Index(context), 0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((2048 + x0) * 16, (2048 + y0) * 16, 1));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((2048 + x1) * 16, (2048 + y1) * 16, 1));

    EndMaskWrite(writer, context);
    list.EndDirect(writer);
}

} // namespace ps2::renderer::gs
