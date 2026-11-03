// ================================================================================================
// File: dma_list.h
// Brief: The display list builder: DMA tags and their VIF/GIF payloads are written into the 16 KiB
//        scratchpad, 8 KiB at a time, and drained by the fromSPR DMA channel into a main-RAM chain
//        ring that the EE itself never touches (Black's allocator). CNT, REF and CALL tags carry
//        the content, so the drained chain is position independent and needs no cache writeback.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

#include <tamtypes.h>

namespace ps2::renderer::dma
{

// The scratchpad: two 8 KiB halves, one being filled while the other drains.
static constexpr u32 kScratchpadBase  = 0x70000000U;
static constexpr u32 kScratchpadBytes = 16U * 1024U;
static constexpr u32 kHalfQwords      = kScratchpadBytes / 2U / 16U;

// VIFcode packing (VIF1: IMMEDIATE 15:0, NUM 23:16, CMD 30:24). A chain runs with TTE, so the
// upper 64 bits of every DMA tag reach VIF1 as two of these.
constexpr u32 VifCode(u32 cmd, u32 num, u32 immediate)
{
    return (immediate & 0xFFFFU) | ((num & 0xFFU) << 16) | ((cmd & 0x7FU) << 24);
}

static constexpr u32 kVifNop    = VifCode(0x00U, 0U, 0U);
static constexpr u32 kVifFlushE = VifCode(0x10U, 0U, 0U); // wait for the microprogram
static constexpr u32 kVifFlush  = VifCode(0x11U, 0U, 0U); // ... and for PATH1/PATH2 to the GS
static constexpr u32 kVifFlushA = VifCode(0x13U, 0U, 0U); // ... and for PATH3
static constexpr u32 kVifMscnt  = VifCode(0x17U, 0U, 0U);

constexpr u32 VifDirect(u32 qwords)
{
    return VifCode(0x50U, 0U, qwords); // 1..65535 qwords of GIF data to PATH2
}

constexpr u32 VifMscal(u32 instructionAddress)
{
    return VifCode(0x14U, 0U, instructionAddress);
}

// A main-RAM destination of the list: one per in-flight frame, 64-byte aligned, written only by
// the fromSPR DMA and read only by the VIF1 DMA.
struct ChainRing
{
    u8 * base;
    u32 bytes;
};

class DisplayList
{
public:
    constexpr DisplayList() = default;

    // Start writing at 'offsetBytes' into 'ring' (0 at a frame start; a mid-frame kick continues
    // where the previous chain ended). Any earlier list must have been Flush()ed.
    void Begin(ChainRing & ring, u32 offsetBytes);

    // True when 'qwords' more fit the ring. Callers check before reserving; Reserve asserts.
    bool Fits(u32 qwords) const;

    // 'qwords' of contiguous scratchpad for a tag and its payload, at most kHalfQwords. A half
    // that cannot hold them drains first; the bytes are committed to the ring as written.
    qword_t * Reserve(u32 qwords);

    // Hand back the unused tail of the last reservation: 'cursor' is where its content ends.
    void Release(qword_t * cursor);

    // CNT: the payload follows the tag; returns where to write it. REF: 'qwords' at 'data'
    // (16-byte aligned) are transferred in place. CALL: run the chain at 'chain' (ending in RET,
    // depth at most two) and come back. END closes the chain. All carry two VIFcodes (TTE).
    qword_t * Cnt(u32 payloadQwords, u32 vif0, u32 vif1);
    void Ref(const void * data, u32 qwords, u32 vif0, u32 vif1);
    void Call(const void * chain, u32 vif0, u32 vif1);
    void End(u32 vif0, u32 vif1);

    // Drain what the scratchpad holds and wait for the fromSPR DMA, so the ring is complete.
    void Flush();

    // Bytes of the ring the list has written or is about to (drained plus pending).
    u32 RingUsedBytes() const;

    u32 SprDrains() const
    {
        return m_sprDrains;
    }

private:
    void Drain();
    qword_t * HalfBase(int half) const;

    ChainRing * m_ring = nullptr;
    u32 m_ringOffset = 0; // bytes already drained into the ring
    int m_half = 0;
    qword_t * m_cursor = nullptr;
    u32 m_sprDrains = 0;
};

} // namespace ps2::renderer::dma
