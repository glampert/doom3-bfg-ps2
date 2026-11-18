// ================================================================================================
// File: scr_print.cpp
// Brief: The crash reports' screen (scr_print.h), from quake2-ps2's rewrite of ps2sdk's debug
//        screen (ee/debug/src/scr_printf.c). Assertion builds only.
//
//  ScrInit takes the GS over, points it at a private frame buffer at the start of VRAM, and from
//  then on blits every glyph there with a HOST->LOCAL transfer pushed by hand down DMA channel 2
//  (the GIF's): no libgraph, libdraw, display list or VU code, which is what a crash screen needs
//  once the renderer has failed. The SDK's register pointers (ee_regs.h) and GIF/GS packers
//  (gif_tags.h, gs_gp.h) spell out the bits its original wrote as magic numbers.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/debug/scr_print.h"

#if PS2_D3BFG_ASSERTS

#include <cstdarg>
#include <cstdio>

#include <draw_tests.h> // ZTEST_METHOD_*
#include <ee_regs.h>    // R_EE_*: the GS privileged and DMAC registers
#include <gif_tags.h>   // GIF_SET_TAG, GIF_REG_AD, GIF_FLG_*, GIF_PRIM_*
#include <gs_gp.h>      // GS_REG_* and GS_SET_*
#include <gs_psm.h>     // GS_PSM_*
#include <kernel.h>     // SetGsCrt, GsPutIMR, UNCACHED_SEG
#include <rom0_info.h>  // GetRomName

namespace ps2::debug
{

// Defined at the end of this file.
extern const u8 g_scrFontBitmap[];

namespace
{

// ------------------------------------------------------------------------------------------------
// The video mode and frame buffer the screen sets up for itself
// ------------------------------------------------------------------------------------------------

// The private frame buffer. The scissor, the clearing sprite, the FRAME and BITBLTBUF widths and the
// character grid in scr_print.h all follow from these.
static constexpr int kFrameBufferWidth = 640;
static constexpr int kFrameBufferHeight = 224;

// FRAME, DISPFB and BITBLTBUF take the buffer width in units of 64 pixels.
static constexpr int kFrameBufferWidthIn64s = kFrameBufferWidth / 64;

static_assert(kScrMaxX == kFrameBufferWidth / (kScrCharSize + 2), "the character grid must match the buffer's width");
static_assert(kScrMaxY == kFrameBufferHeight / (kScrCharSize + 2), "the character grid must match the buffer's height");

// VRAM pages of 8 KiB. The buffer takes 640 x 224 x 4 bytes, 70 pages from page 0. The depth buffer
// sits far past it: nothing is depth tested, but ZBUF_1 must still be valid.
static constexpr int kFrameBufferBasePage = 0;
static constexpr int kZBufferBasePage = 140;

// XYOFFSET_1 puts the drawing window's corner inside the GS's 4096 x 4096 space; the spot is the SDK
// debug screen's, and every vertex is biased by it. Both are 12.4 fixed point.
static constexpr int kDrawOriginX = 1728;
static constexpr int kDrawOriginY = 1936;

constexpr int ToFixed12_4(int pixels)
{
    return pixels * 16;
}

// SetGsCrt's mode and libgraph's picture origin for each interlaced standard (graph_mode.c), so the
// text sits where the game's own picture does. In FIELD mode each field's lines land on alternate
// raster lines: the origin doubles, less the first line, as graph_set_screen computes it.
struct StandardDisplay
{
    s16 crtMode;
    u32 displayX;
    u32 displayY;
};

static constexpr StandardDisplay kNtscDisplay = { 2, 652, (26 - 1) * 2 };
static constexpr StandardDisplay kPalDisplay = { 3, 680, (37 - 1) * 2 };

// The GS DISPLAY register: DX, DY, MAGH and MAGV (less one), then DW and DH (less one).
constexpr u64 MakeDisplay(u32 dx, u32 dy, u32 magh, u32 magv, u32 dw, u32 dh)
{
    return static_cast<u64>(dx & 0xFFFu) | (static_cast<u64>(dy & 0x7FFu) << 12) | (static_cast<u64>(magh & 0xFu) << 23) |
           (static_cast<u64>(magv & 0x3u) << 27) | (static_cast<u64>(dw & 0xFFFu) << 32) | (static_cast<u64>(dh & 0x7FFu) << 44);
}

static_assert(MakeDisplay(636, 50, 3, 1, 2559, 447) == 0x001BF9FF0983227Cull, "MakeDisplay must pack the SDK debug screen's value");

// ------------------------------------------------------------------------------------------------
// GIF packets
//
// A GIF tag is one qword: NLOOP, EOP, FLG and NREG in the low 64 bits, the register list in the high
// 64. In PACKED mode with one A+D descriptor, each of the NLOOP qwords after it writes one register.
// ------------------------------------------------------------------------------------------------

struct GifRegWrite
{
    u64 data;
    u64 regAddr;
};

static constexpr int kSetupRegCount = 14;
static constexpr int kGlyphRegCount = 4;

// Sent once by ScrInit: the whole drawing environment, then one black sprite over the whole buffer
// to clear whatever was on screen.
struct SetupPacket
{
    u64 gifTag;
    u64 gifTagRegs;
    GifRegWrite regs[kSetupRegCount];
};

// One 8x8 glyph expanded to 32 bits: 256 bytes, 16 qwords.
static constexpr int kGlyphPixelQwords = static_cast<int>((kScrCharSize * kScrCharSize * sizeof(u32)) / 16);

// Sent before every glyph: a HOST->LOCAL transfer of one 8x8 block to the glyph's cell, then an IMAGE
// mode tag the pixel qwords DMA'd right behind it fill.
struct GlyphPacket
{
    u64 gifTag;
    u64 gifTagRegs;
    GifRegWrite bitBltBuf; // the destination: our frame buffer
    GifRegWrite trxPos;    // the destination corner, patched per glyph by ScrPrintChar
    GifRegWrite trxReg;    // the transfer's size, one glyph
    GifRegWrite trxDir;    // the direction; writing it starts the transfer
    u64 pixelsGifTag;      // the IMAGE tag for the pixel qwords that follow
    u64 pixelsGifTagRegs;  // its descriptor half, unused in IMAGE mode
};

static_assert(sizeof(SetupPacket) % 16 == 0, "GIF packets are whole qwords");
static_assert(sizeof(GlyphPacket) % 16 == 0, "GIF packets are whole qwords");

static constexpr int kSetupPacketQwords = static_cast<int>(sizeof(SetupPacket) / 16);
static constexpr int kGlyphPacketQwords = static_cast<int>(sizeof(GlyphPacket) / 16);

// ------------------------------------------------------------------------------------------------
// State
// ------------------------------------------------------------------------------------------------

static bool s_isInit = false;
static bool s_hasStandard = false;
static system::video::Standard s_standard = system::video::Standard::Ntsc;
static int s_currX = 0;
static int s_currY = 0;
static u32 s_textColor = 0xFFFFFFFF; // white
static u32 s_bgColor = 0x00000000;   // black

// Never patched after startup, so it lives in read-only memory.
alignas(16) static const SetupPacket s_setupPacket = {
    GIF_SET_TAG(kSetupRegCount, /* EOP = */ 1, 0, 0, GIF_FLG_PACKED, /* NREG = */ 1),
    GIF_REG_AD,
    {
        // Our 32-bit buffer at the very start of VRAM, no write mask.
        { GS_SET_FRAME(kFrameBufferBasePage, kFrameBufferWidthIn64s, GS_PSM_32, 0), GS_REG_FRAME_1 },
        { GS_SET_ZBUF(kZBufferBasePage, GS_PSMZ_32, /* ZMSK = */ 0), GS_REG_ZBUF_1 },

        // The window inside the drawing space, everything clipped to it.
        { GS_SET_XYOFFSET(ToFixed12_4(kDrawOriginX), ToFixed12_4(kDrawOriginY)), GS_REG_XYOFFSET_1 },
        { GS_SET_SCISSOR(0, kFrameBufferWidth - 1, 0, kFrameBufferHeight - 1), GS_REG_SCISSOR_1 },

        { GS_SET_PRMODECONT(1), GS_REG_PRMODECONT }, // primitive state from PRIM, not PRMODE
        { GS_SET_COLCLAMP(1), GS_REG_COLCLAMP },     // clamp, don't wrap
        { GS_SET_DTHE(0), GS_REG_DTHE },             // no dithering

        // ZTE must stay 1 (the GS cannot turn depth testing off), so the method is what changes:
        // GREATER at rest, ALLPASS for the clearing sprite over a never-cleared depth buffer.
        { GS_SET_TEST(0, 0, 0, 0, 0, 0, /* ZTE = */ 1, ZTEST_METHOD_GREATER), GS_REG_TEST_1 },
        { GS_SET_TEST(0, 0, 0, 0, 0, 0, /* ZTE = */ 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST_1 },

        // One opaque black sprite over the whole buffer, its corners biased by the origin.
        { GS_SET_PRIM(GIF_PRIM_SPRITE, 0, 0, 0, 0, 0, 0, 0, 0), GS_REG_PRIM },
        { GS_SET_RGBAQ(0, 0, 0, 0, /* Q = 1.0f */ 0x3F800000), GS_REG_RGBAQ },
        { GS_SET_XYZ(ToFixed12_4(kDrawOriginX), ToFixed12_4(kDrawOriginY), 0), GS_REG_XYZ2 },
        { GS_SET_XYZ(ToFixed12_4(kDrawOriginX + kFrameBufferWidth), ToFixed12_4(kDrawOriginY + kFrameBufferHeight), 0), GS_REG_XYZ2 },

        // The depth test as it was before the clear.
        { GS_SET_TEST(0, 0, 0, 0, 0, 0, /* ZTE = */ 1, ZTEST_METHOD_GREATER), GS_REG_TEST_1 },
    }
};

// Patched in place, through the uncached segment, by every ScrPrintChar.
alignas(16) static GlyphPacket s_glyphPacket = {
    GIF_SET_TAG(kGlyphRegCount, /* EOP = */ 0, 0, 0, GIF_FLG_PACKED, /* NREG = */ 1),
    GIF_REG_AD,
    { GS_SET_BITBLTBUF(0, 0, 0, kFrameBufferBasePage, kFrameBufferWidthIn64s, GS_PSM_32), GS_REG_BITBLTBUF },
    { GS_SET_TRXPOS(0, 0, /* DSAX = */ 0, /* DSAY = */ 0, 0), GS_REG_TRXPOS },
    { GS_SET_TRXREG(kScrCharSize, kScrCharSize), GS_REG_TRXREG },
    { GS_SET_TRXDIR(/* HOST -> LOCAL */ 0), GS_REG_TRXDIR },
    GIF_SET_TAG(kGlyphPixelQwords, /* EOP = */ 0, 0, 0, GIF_FLG_IMAGE, /* NREG = */ 0),
    0
};

// One glyph expanded from the 1-bit font to 32-bit pixels, DMA'd right behind s_glyphPacket.
alignas(16) static u32 s_glyphPixels[kScrCharSize * kScrCharSize];

// ------------------------------------------------------------------------------------------------
// GS and DMAC plumbing
// ------------------------------------------------------------------------------------------------

// Dn_CHCR (the DMAC chapter of the EE User's Manual).
static constexpr u32 kDmaChcrDirFromMemory = 1u << 0; // DIR: 1 from memory
static constexpr u32 kDmaChcrStart = 1u << 8;         // STR: set to start, cleared when done

// D_CTRL.DMAE: the global enable.
static constexpr u32 kDmaCtrlEnable = 1u << 0;

// D_ENABLER/D_ENABLEW.CPND: suspend every channel.
static constexpr u32 kDmaEnableSuspend = 1u << 16;

// VIF1_FBRST.RST, GIF_CTRL.RST, and VU1's reset in VU0 control register 28 (FBRST).
static constexpr u32 kVif1FbrstReset = 1u << 0;
static constexpr u32 kGifCtrlReset = 1u << 0;
static constexpr u32 kVuFbrstResetVu1 = 1u << 9;

// COP0 Status.CU2: COP2 (VU0 macro mode) instructions usable.
static constexpr u32 kCop0StatusCu2 = 1u << 30;

// D_STAT status bits of every channel but SIF0/1/2 (bits 5-7), which the IOP RPC layer owns, plus
// the stall, MFIFO-empty and bus-error bits.
static constexpr u32 kDmaStatNonSifBits = 0xFF1F;

// D2_SADR, which has no R_EE_ macro but which Sony's DMAC init and the SDK debug screen both write.
static constexpr u32 kD2SadrAddr = 0x1000A080;

// Soft-resets the GS and programs the CRT controller.
void ResetGs(s16 interlace, s16 videoMode, s16 fieldMode)
{
    *R_EE_GS_CSR = 0x200; // CSR.RESET
    GsPutIMR(0xFF00);     // every GS interrupt masked: this polls
    SetGsCrt(interlace, videoMode, fieldMode);
}

// Read circuit 2 shows our buffer over the visible raster, at the standard's picture origin.
void SetVideoMode(const StandardDisplay & display)
{
    // EN2 only; the output alpha from ALP (0xFF), not the buffer.
    *R_EE_GS_PMODE = 0xFF62;

    // INT 1, FFMD 0 (FIELD: each field reads every other line), over SetGsCrt's FRAME.
    *R_EE_GS_SMODE2 = 1;

    // FBP 0, FBW 10 (640), PSMCT32.
    *R_EE_GS_DISPFB2 = 0x1400;

    // MAGH 4x and MAGV 2x stretch 640 x 224 over DW 2560 and DH 448: all of NTSC's lines, PAL's
    // top 448 of 512.
    *R_EE_GS_DISPLAY2 = MakeDisplay(display.displayX, display.displayY, 3, 1, 2559, 447);
}

// Frees every path into the GS before the screen takes PATH3. A fatal error can land while the
// renderer's chain is in flight, or because it wedged (a VU1 program that never ends, a GIF packet
// that never reaches EOP); then the GIF never grants PATH3 and the first DmaWaitGif spins forever
// on a screen ResetGs blanked. The order is libgs's GsResetPath (ee/libgs/src/ResetPath.s): VIF1,
// VU1, then the GIF, each after what feeds it, and the DMA channels first for the same reason.
void ResetGraphicsPaths()
{
    // Stop the VIF1 chain and any GIF transfer: the rest of an interrupted upload would reach the
    // reset GIF as tags. A running channel stops only with the DMAC suspended.
    const u32 dmaEnable = *R_EE_D_ENABLER;
    *R_EE_D_ENABLEW = dmaEnable | kDmaEnableSuspend;
    *R_EE_D1_CHCR = 0;
    *R_EE_D2_CHCR = 0;
    *R_EE_D_ENABLEW = dmaEnable;

    // VIF1 first, so it starts no further microprogram or DIRECT transfer.
    *R_EE_VIF1_FBRST = kVif1FbrstReset;

    // VU1, or a running program would XGKICK again (and one stalled in an XGKICK resume with half a
    // packet). Its reset is in VU0's control register 28, which needs COP2: without it this would
    // fault and turn the report into an exception report. Read-modify-write, as libgs does.
    u32 cop0Status = 0;
    asm volatile("mfc0 %0, $12" : "=r"(cop0Status));

    if ((cop0Status & kCop0StatusCu2) != 0)
    {
        u32 vuFbrst = 0;
        asm volatile("cfc2 %0, $28" : "=r"(vuFbrst));
        vuFbrst |= kVuFbrstResetVu1;
        asm volatile("ctc2 %0, $28 \n\t"
                     "sync.p       \n\t"
                     :
                     : "r"(vuFbrst)
                     : "memory");
    }

    // The GIF last: it drops the open path and anything queued.
    *R_EE_GIF_CTRL = kGifCtrlReset;
}

// Resets channel 2 and the global DMAC registers, so the GIF can be driven by hand whatever state
// the renderer left the DMAC in. Modelled on Sony's DMAC init, hence the parts that look redundant.
void DmaReset()
{
    *reinterpret_cast<vu32 *>(kD2SadrAddr) = 0;

    // The SDK routine this follows clears channel 3's CHCR here and never D2_CHCR, which current
    // ps2sdk reads as a typo. Kept: ResetGraphicsPaths stopped channel 2 already.
    *R_EE_D3_CHCR = 0;

    *R_EE_D2_TADR = 0;
    *R_EE_D2_MADR = 0;
    *R_EE_D2_ASR1 = 0;
    *R_EE_D2_ASR0 = 0;

    // A 1 clears a D_STAT status bit but toggles a mask bit (16-31): both writes stay in the low
    // half, so the masks, and the SIF channels, come out as they were.
    *R_EE_D_STAT = kDmaStatNonSifBits;
    *R_EE_D_STAT &= kDmaStatNonSifBits;

    *R_EE_D_CTRL = 0;
    *R_EE_D_PCR = 0;
    *R_EE_D_SQWC = 0;
    *R_EE_D_RBOR = 0;
    *R_EE_D_RBSR = 0;

    *R_EE_D_CTRL |= kDmaCtrlEnable;
}

// A normal-mode transfer into the GIF: 16-byte aligned data, qword count. Returns once it starts.
void DmaSendToGif(const void * data, int qwordCount)
{
    *R_EE_D2_QWC = static_cast<u32>(qwordCount);
    *R_EE_D2_MADR = reinterpret_cast<u32>(data);
    *R_EE_D2_CHCR = kDmaChcrStart | kDmaChcrDirFromMemory;
}

void DmaWaitGif()
{
    // Busy: this runs on crash paths only.
    while ((*R_EE_D2_CHCR & kDmaChcrStart) != 0)
    {
    }
}

// The cursor to the start of the next line, which is cleared.
void NextLine()
{
    s_currX = 0;
    ++s_currY;

    if (s_currY == kScrMaxY)
    {
        s_currY = 0;
    }

    ScrClearLine(s_currY);
}

PS2_COLD_FUNC system::video::Standard RomStandard()
{
    char romName[16] = {};
    GetRomName(romName);
    return romName[4] == 'E' ? system::video::Standard::Pal : system::video::Standard::Ntsc;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Printing
// ------------------------------------------------------------------------------------------------

void ScrSetStandard(system::video::Standard standard)
{
    s_standard = standard;
    s_hasStandard = true;
}

PS2_COLD_FUNC void ScrInit()
{
    ResetGraphicsPaths();
    DmaReset();

    const system::video::Standard standard = s_hasStandard ? s_standard : RomStandard();
    const StandardDisplay & display = standard == system::video::Standard::Pal ? kPalDisplay : kNtscDisplay;

    // Interlaced; SetGsCrt's FRAME, which SetVideoMode turns to FIELD.
    ResetGs(1, display.crtMode, 1);
    SetVideoMode(display);

    DmaWaitGif();
    DmaSendToGif(&s_setupPacket, kSetupPacketQwords);
    DmaWaitGif();

    s_isInit = true;
}

PS2_COLD_FUNC void ScrPrintChar(int x, int y, u32 color, int ch)
{
    if (x < 0 || x >= kScrMaxX || y < 0 || y >= kScrMaxY)
    {
        return;
    }

    if (!s_isInit)
    {
        ScrInit();
    }

    // The GIF reads both straight from RAM: every store goes through the uncached segment.
    auto * const glyphPacket = static_cast<GlyphPacket *>(UNCACHED_SEG(&s_glyphPacket));
    auto * const glyphPixels = static_cast<u32 *>(UNCACHED_SEG(s_glyphPixels));

    // A two-pixel margin at the sides and between characters.
    const int destX = x * (kScrCharSize + 2) + 2;
    const int destY = y * (kScrCharSize + 2) + 2;
    glyphPacket->trxPos.data = GS_SET_TRXPOS(0, 0, destX, destY, 0);

    // The transfer setup goes out while the glyph expands.
    DmaSendToGif(&s_glyphPacket, kGlyphPacketQwords);

    // Bit 7 of each font byte is the row's leftmost pixel; unset bits take the background, so the
    // blit also erases whatever the cell held.
    const u8 * fontRow = &g_scrFontBitmap[(ch & 0xFF) * kScrCharSize];

    for (int row = 0; row < kScrCharSize; ++row, ++fontRow)
    {
        for (int col = 0; col < kScrCharSize; ++col)
        {
            const bool lit = (*fontRow & (0x80 >> col)) != 0;
            glyphPixels[row * kScrCharSize + col] = lit ? color : s_bgColor;
        }
    }

    DmaWaitGif();
    DmaSendToGif(s_glyphPixels, kGlyphPixelQwords);
    DmaWaitGif();
}

PS2_COLD_FUNC void ScrPrintf(const char * format, ...)
{
    if (!s_isInit)
    {
        ScrInit();
    }

    // Static, to keep a crash path's stack small.
    static char s_text[2048];
    va_list args;
    va_start(args, format);
    int length = std::vsnprintf(s_text, sizeof(s_text), format, args);
    va_end(args);

    // vsnprintf returns the untruncated length.
    if (length >= static_cast<int>(sizeof(s_text)))
    {
        length = static_cast<int>(sizeof(s_text)) - 1;
    }

    for (int i = 0; i < length; ++i)
    {
        const int c = static_cast<u8>(s_text[i]);

        switch (c)
        {
            case '\n':
                NextLine();
                break;

            case '\t':
                // Four spaces, stopping at the edge.
                for (int j = 0; j < 4 && s_currX < kScrMaxX; ++j)
                {
                    ScrPrintChar(s_currX, s_currY, s_textColor, ' ');
                    ++s_currX;
                }

                break;

            default:
                // Wrap lazily, so a full line followed by '\n' leaves no blank row.
                if (s_currX == kScrMaxX)
                {
                    NextLine();
                }

                ScrPrintChar(s_currX, s_currY, s_textColor, c);
                ++s_currX;
                break;
        }
    }
}

int ScrGetPrintPosX()
{
    return s_currX;
}

int ScrGetPrintPosY()
{
    return s_currY;
}

void ScrSetPrintPos(int x, int y)
{
    if (x >= 0 && x < kScrMaxX)
    {
        s_currX = x;
    }

    if (y >= 0 && y < kScrMaxY)
    {
        s_currY = y;
    }
}

void ScrSetBgColor(u32 color)
{
    s_bgColor = color;
}

u32 ScrGetBgColor()
{
    return s_bgColor;
}

void ScrSetTextColor(u32 color)
{
    s_textColor = color;
}

u32 ScrGetTextColor()
{
    return s_textColor;
}

PS2_COLD_FUNC void ScrClear()
{
    for (int y = 0; y < kScrMaxY; ++y)
    {
        ScrClearLine(y);
    }

    s_currX = 0;
    s_currY = 0;
}

PS2_COLD_FUNC void ScrClearLine(int y)
{
    for (int x = 0; x < kScrMaxX; ++x)
    {
        ScrPrintChar(x, y, s_bgColor, ' ');
    }
}

// ------------------------------------------------------------------------------------------------
// The font: 256 characters of 8 bytes, a byte per row, a bit per pixel, MSB first
// ------------------------------------------------------------------------------------------------

alignas(16) const u8 g_scrFontBitmap[] =
"\x00\x00\x00\x00\x00\x00\x00\x00\x3c\x42\xa5\x81\xa5\x99\x42\x3c"
"\x3c\x7e\xdb\xff\xff\xdb\x66\x3c\x6c\xfe\xfe\xfe\x7c\x38\x10\x00"
"\x10\x38\x7c\xfe\x7c\x38\x10\x00\x10\x38\x54\xfe\x54\x10\x38\x00"
"\x10\x38\x7c\xfe\xfe\x10\x38\x00\x00\x00\x00\x30\x30\x00\x00\x00"
"\xff\xff\xff\xe7\xe7\xff\xff\xff\x38\x44\x82\x82\x82\x44\x38\x00"
"\xc7\xbb\x7d\x7d\x7d\xbb\xc7\xff\x0f\x03\x05\x79\x88\x88\x88\x70"
"\x38\x44\x44\x44\x38\x10\x7c\x10\x30\x28\x24\x24\x28\x20\xe0\xc0"
"\x3c\x24\x3c\x24\x24\xe4\xdc\x18\x10\x54\x38\xee\x38\x54\x10\x00"
"\x10\x10\x10\x7c\x10\x10\x10\x10\x10\x10\x10\xff\x00\x00\x00\x00"
"\x00\x00\x00\xff\x10\x10\x10\x10\x10\x10\x10\xf0\x10\x10\x10\x10"
"\x10\x10\x10\x1f\x10\x10\x10\x10\x10\x10\x10\xff\x10\x10\x10\x10"
"\x10\x10\x10\x10\x10\x10\x10\x10\x00\x00\x00\xff\x00\x00\x00\x00"
"\x00\x00\x00\x1f\x10\x10\x10\x10\x00\x00\x00\xf0\x10\x10\x10\x10"
"\x10\x10\x10\x1f\x00\x00\x00\x00\x10\x10\x10\xf0\x00\x00\x00\x00"
"\x81\x42\x24\x18\x18\x24\x42\x81\x01\x02\x04\x08\x10\x20\x40\x80"
"\x80\x40\x20\x10\x08\x04\x02\x01\x00\x10\x10\xff\x10\x10\x00\x00"
"\x00\x00\x00\x00\x00\x00\x00\x00\x20\x20\x20\x20\x00\x00\x20\x00"
"\x50\x50\x50\x00\x00\x00\x00\x00\x50\x50\xf8\x50\xf8\x50\x50\x00"
"\x20\x78\xa0\x70\x28\xf0\x20\x00\xc0\xc8\x10\x20\x40\x98\x18\x00"
"\x40\xa0\x40\xa8\x90\x98\x60\x00\x10\x20\x40\x00\x00\x00\x00\x00"
"\x10\x20\x40\x40\x40\x20\x10\x00\x40\x20\x10\x10\x10\x20\x40\x00"
"\x20\xa8\x70\x20\x70\xa8\x20\x00\x00\x20\x20\xf8\x20\x20\x00\x00"
"\x00\x00\x00\x00\x00\x20\x20\x40\x00\x00\x00\x78\x00\x00\x00\x00"
"\x00\x00\x00\x00\x00\x60\x60\x00\x00\x00\x08\x10\x20\x40\x80\x00"
"\x70\x88\x98\xa8\xc8\x88\x70\x00\x20\x60\xa0\x20\x20\x20\xf8\x00"
"\x70\x88\x08\x10\x60\x80\xf8\x00\x70\x88\x08\x30\x08\x88\x70\x00"
"\x10\x30\x50\x90\xf8\x10\x10\x00\xf8\x80\xe0\x10\x08\x10\xe0\x00"
"\x30\x40\x80\xf0\x88\x88\x70\x00\xf8\x88\x10\x20\x20\x20\x20\x00"
"\x70\x88\x88\x70\x88\x88\x70\x00\x70\x88\x88\x78\x08\x10\x60\x00"
"\x00\x00\x20\x00\x00\x20\x00\x00\x00\x00\x20\x00\x00\x20\x20\x40"
"\x18\x30\x60\xc0\x60\x30\x18\x00\x00\x00\xf8\x00\xf8\x00\x00\x00"
"\xc0\x60\x30\x18\x30\x60\xc0\x00\x70\x88\x08\x10\x20\x00\x20\x00"
"\x70\x88\x08\x68\xa8\xa8\x70\x00\x20\x50\x88\x88\xf8\x88\x88\x00"
"\xf0\x48\x48\x70\x48\x48\xf0\x00\x30\x48\x80\x80\x80\x48\x30\x00"
"\xe0\x50\x48\x48\x48\x50\xe0\x00\xf8\x80\x80\xf0\x80\x80\xf8\x00"
"\xf8\x80\x80\xf0\x80\x80\x80\x00\x70\x88\x80\xb8\x88\x88\x70\x00"
"\x88\x88\x88\xf8\x88\x88\x88\x00\x70\x20\x20\x20\x20\x20\x70\x00"
"\x38\x10\x10\x10\x90\x90\x60\x00\x88\x90\xa0\xc0\xa0\x90\x88\x00"
"\x80\x80\x80\x80\x80\x80\xf8\x00\x88\xd8\xa8\xa8\x88\x88\x88\x00"
"\x88\xc8\xc8\xa8\x98\x98\x88\x00\x70\x88\x88\x88\x88\x88\x70\x00"
"\xf0\x88\x88\xf0\x80\x80\x80\x00\x70\x88\x88\x88\xa8\x90\x68\x00"
"\xf0\x88\x88\xf0\xa0\x90\x88\x00\x70\x88\x80\x70\x08\x88\x70\x00"
"\xf8\x20\x20\x20\x20\x20\x20\x00\x88\x88\x88\x88\x88\x88\x70\x00"
"\x88\x88\x88\x88\x50\x50\x20\x00\x88\x88\x88\xa8\xa8\xd8\x88\x00"
"\x88\x88\x50\x20\x50\x88\x88\x00\x88\x88\x88\x70\x20\x20\x20\x00"
"\xf8\x08\x10\x20\x40\x80\xf8\x00\x70\x40\x40\x40\x40\x40\x70\x00"
"\x00\x00\x80\x40\x20\x10\x08\x00\x70\x10\x10\x10\x10\x10\x70\x00"
"\x20\x50\x88\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf8\x00"
"\x40\x20\x10\x00\x00\x00\x00\x00\x00\x00\x70\x08\x78\x88\x78\x00"
"\x80\x80\xb0\xc8\x88\xc8\xb0\x00\x00\x00\x70\x88\x80\x88\x70\x00"
"\x08\x08\x68\x98\x88\x98\x68\x00\x00\x00\x70\x88\xf8\x80\x70\x00"
"\x10\x28\x20\xf8\x20\x20\x20\x00\x00\x00\x68\x98\x98\x68\x08\x70"
"\x80\x80\xf0\x88\x88\x88\x88\x00\x20\x00\x60\x20\x20\x20\x70\x00"
"\x10\x00\x30\x10\x10\x10\x90\x60\x40\x40\x48\x50\x60\x50\x48\x00"
"\x60\x20\x20\x20\x20\x20\x70\x00\x00\x00\xd0\xa8\xa8\xa8\xa8\x00"
"\x00\x00\xb0\xc8\x88\x88\x88\x00\x00\x00\x70\x88\x88\x88\x70\x00"
"\x00\x00\xb0\xc8\xc8\xb0\x80\x80\x00\x00\x68\x98\x98\x68\x08\x08"
"\x00\x00\xb0\xc8\x80\x80\x80\x00\x00\x00\x78\x80\xf0\x08\xf0\x00"
"\x40\x40\xf0\x40\x40\x48\x30\x00\x00\x00\x90\x90\x90\x90\x68\x00"
"\x00\x00\x88\x88\x88\x50\x20\x00\x00\x00\x88\xa8\xa8\xa8\x50\x00"
"\x00\x00\x88\x50\x20\x50\x88\x00\x00\x00\x88\x88\x98\x68\x08\x70"
"\x00\x00\xf8\x10\x20\x40\xf8\x00\x18\x20\x20\x40\x20\x20\x18\x00"
"\x20\x20\x20\x00\x20\x20\x20\x00\xc0\x20\x20\x10\x20\x20\xc0\x00"
"\x40\xa8\x10\x00\x00\x00\x00\x00\x00\x00\x20\x50\xf8\x00\x00\x00"
"\x70\x88\x80\x80\x88\x70\x20\x60\x90\x00\x00\x90\x90\x90\x68\x00"
"\x10\x20\x70\x88\xf8\x80\x70\x00\x20\x50\x70\x08\x78\x88\x78\x00"
"\x48\x00\x70\x08\x78\x88\x78\x00\x20\x10\x70\x08\x78\x88\x78\x00"
"\x20\x00\x70\x08\x78\x88\x78\x00\x00\x70\x80\x80\x80\x70\x10\x60"
"\x20\x50\x70\x88\xf8\x80\x70\x00\x50\x00\x70\x88\xf8\x80\x70\x00"
"\x20\x10\x70\x88\xf8\x80\x70\x00\x50\x00\x00\x60\x20\x20\x70\x00"
"\x20\x50\x00\x60\x20\x20\x70\x00\x40\x20\x00\x60\x20\x20\x70\x00"
"\x50\x00\x20\x50\x88\xf8\x88\x00\x20\x00\x20\x50\x88\xf8\x88\x00"
"\x10\x20\xf8\x80\xf0\x80\xf8\x00\x00\x00\x6c\x12\x7e\x90\x6e\x00"
"\x3e\x50\x90\x9c\xf0\x90\x9e\x00\x60\x90\x00\x60\x90\x90\x60\x00"
"\x90\x00\x00\x60\x90\x90\x60\x00\x40\x20\x00\x60\x90\x90\x60\x00"
"\x40\xa0\x00\xa0\xa0\xa0\x50\x00\x40\x20\x00\xa0\xa0\xa0\x50\x00"
"\x90\x00\x90\x90\xb0\x50\x10\xe0\x50\x00\x70\x88\x88\x88\x70\x00"
"\x50\x00\x88\x88\x88\x88\x70\x00\x20\x20\x78\x80\x80\x78\x20\x20"
"\x18\x24\x20\xf8\x20\xe2\x5c\x00\x88\x50\x20\xf8\x20\xf8\x20\x00"
"\xc0\xa0\xa0\xc8\x9c\x88\x88\x8c\x18\x20\x20\xf8\x20\x20\x20\x40"
"\x10\x20\x70\x08\x78\x88\x78\x00\x10\x20\x00\x60\x20\x20\x70\x00"
"\x20\x40\x00\x60\x90\x90\x60\x00\x20\x40\x00\x90\x90\x90\x68\x00"
"\x50\xa0\x00\xa0\xd0\x90\x90\x00\x28\x50\x00\xc8\xa8\x98\x88\x00"
"\x00\x70\x08\x78\x88\x78\x00\xf8\x00\x60\x90\x90\x90\x60\x00\xf0"
"\x20\x00\x20\x40\x80\x88\x70\x00\x00\x00\x00\xf8\x80\x80\x00\x00"
"\x00\x00\x00\xf8\x08\x08\x00\x00\x84\x88\x90\xa8\x54\x84\x08\x1c"
"\x84\x88\x90\xa8\x58\xa8\x3c\x08\x20\x00\x00\x20\x20\x20\x20\x00"
"\x00\x00\x24\x48\x90\x48\x24\x00\x00\x00\x90\x48\x24\x48\x90\x00"
"\x28\x50\x20\x50\x88\xf8\x88\x00\x28\x50\x70\x08\x78\x88\x78\x00"
"\x28\x50\x00\x70\x20\x20\x70\x00\x28\x50\x00\x20\x20\x20\x70\x00"
"\x28\x50\x00\x70\x88\x88\x70\x00\x50\xa0\x00\x60\x90\x90\x60\x00"
"\x28\x50\x00\x88\x88\x88\x70\x00\x50\xa0\x00\xa0\xa0\xa0\x50\x00"
"\xfc\x48\x48\x48\xe8\x08\x50\x20\x00\x50\x00\x50\x50\x50\x10\x20"
"\xc0\x44\xc8\x54\xec\x54\x9e\x04\x10\xa8\x40\x00\x00\x00\x00\x00"
"\x00\x20\x50\x88\x50\x20\x00\x00\x88\x10\x20\x40\x80\x28\x00\x00"
"\x7c\xa8\xa8\x68\x28\x28\x28\x00\x38\x40\x30\x48\x48\x30\x08\x70"
"\x00\x00\x00\x00\x00\x00\xff\xff\xf0\xf0\xf0\xf0\x0f\x0f\x0f\x0f"
"\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x3c\x3c\x00\x00\x00\xff\xff\xff\xff\xff\xff\x00\x00"
"\xc0\xc0\xc0\xc0\xc0\xc0\xc0\xc0\x0f\x0f\x0f\x0f\xf0\xf0\xf0\xf0"
"\xfc\xfc\xfc\xfc\xfc\xfc\xfc\xfc\x03\x03\x03\x03\x03\x03\x03\x03"
"\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x3f\x11\x22\x44\x88\x11\x22\x44\x88"
"\x88\x44\x22\x11\x88\x44\x22\x11\xfe\x7c\x38\x10\x00\x00\x00\x00"
"\x00\x00\x00\x00\x10\x38\x7c\xfe\x80\xc0\xe0\xf0\xe0\xc0\x80\x00"
"\x01\x03\x07\x0f\x07\x03\x01\x00\xff\x7e\x3c\x18\x18\x3c\x7e\xff"
"\x81\xc3\xe7\xff\xff\xe7\xc3\x81\xf0\xf0\xf0\xf0\x00\x00\x00\x00"
"\x00\x00\x00\x00\x0f\x0f\x0f\x0f\x0f\x0f\x0f\x0f\x00\x00\x00\x00"
"\x00\x00\x00\x00\xf0\xf0\xf0\xf0\x33\x33\xcc\xcc\x33\x33\xcc\xcc"
"\x00\x20\x20\x50\x50\x88\xf8\x00\x20\x20\x70\x20\x70\x20\x20\x00"
"\x00\x00\x00\x50\x88\xa8\x50\x00\xff\xff\xff\xff\xff\xff\xff\xff"
"\x00\x00\x00\x00\xff\xff\xff\xff\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0"
"\x0f\x0f\x0f\x0f\x0f\x0f\x0f\x0f\xff\xff\xff\xff\x00\x00\x00\x00"
"\x00\x00\x68\x90\x90\x90\x68\x00\x30\x48\x48\x70\x48\x48\x70\xc0"
"\xf8\x88\x80\x80\x80\x80\x80\x00\xf8\x50\x50\x50\x50\x50\x98\x00"
"\xf8\x88\x40\x20\x40\x88\xf8\x00\x00\x00\x78\x90\x90\x90\x60\x00"
"\x00\x50\x50\x50\x50\x68\x80\x80\x00\x50\xa0\x20\x20\x20\x20\x00"
"\xf8\x20\x70\xa8\xa8\x70\x20\xf8\x20\x50\x88\xf8\x88\x50\x20\x00"
"\x70\x88\x88\x88\x50\x50\xd8\x00\x30\x40\x40\x20\x50\x50\x50\x20"
"\x00\x00\x00\x50\xa8\xa8\x50\x00\x08\x70\xa8\xa8\xa8\x70\x80\x00"
"\x38\x40\x80\xf8\x80\x40\x38\x00\x70\x88\x88\x88\x88\x88\x88\x00"
"\x00\xf8\x00\xf8\x00\xf8\x00\x00\x20\x20\xf8\x20\x20\x00\xf8\x00"
"\xc0\x30\x08\x30\xc0\x00\xf8\x00\x18\x60\x80\x60\x18\x00\xf8\x00"
"\x10\x28\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\xa0\x40"
"\x00\x20\x00\xf8\x00\x20\x00\x00\x00\x50\xa0\x00\x50\xa0\x00\x00"
"\x00\x18\x24\x24\x18\x00\x00\x00\x00\x30\x78\x78\x30\x00\x00\x00"
"\x00\x00\x00\x00\x30\x00\x00\x00\x3e\x20\x20\x20\xa0\x60\x20\x00"
"\xa0\x50\x50\x50\x00\x00\x00\x00\x40\xa0\x20\x40\xe0\x00\x00\x00"
"\x00\x38\x38\x38\x38\x38\x38\x00\x00\x00\x00\x00\x00\x00\x00";

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
