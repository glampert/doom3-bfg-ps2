// ================================================================================================
// File: dma_list.h
// Brief: The display list, the one interface draw code builds a frame's commands through: DIRECT
//        GIF blocks, path fences, VU1 frame constants, program binds and batches. The DMA tags and
//        their VIF/GIF payloads are written into the 16 KiB scratchpad, 8 KiB at a time, and drained
//        by the fromSPR DMA channel into a main-RAM chain ring that the EE itself never touches
//        (Black's allocator). CNT, REF and CALL tags carry the content, so the drained chain is
//        position independent and needs no cache writeback.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/gif_writer.h"
#include "ps2/renderer/vu.h"
#include <tamtypes.h>

namespace ps2::renderer::dma
{

// The scratchpad: two 8 KiB halves, one being filled while the other drains.
static constexpr u32 kScratchpadBase  = 0x70000000U;
static constexpr u32 kScratchpadBytes = 16U * 1024U;
static constexpr u32 kHalfQwords      = kScratchpadBytes / 2U / 16U;

// A main-RAM destination of the list: one per in-flight frame, 64-byte aligned, written only by
// the fromSPR DMA and read only by the VIF1 DMA.
struct ChainRing
{
    u8 * base;
    u32 bytes;
};

class DisplayList final
{
public:
    constexpr DisplayList() = default;
    DisplayList(const DisplayList & other) = delete;
    DisplayList & operator=(const DisplayList & other) = delete;

    // ---- Commands ----

    // Each reserves its own room: a ring that cannot hold it kicks, waits and restarts.

    // A CNT block whose payload is GIF data sent through DIRECT. Every block is closed before any
    // other command, so no reservation can drain or invalidate its writer.
    gs::GifWriter BeginDirect(int payloadQwords);
    void EndDirect(const gs::GifWriter & writer);

    // VIF FLUSH: later commands wait for the running microprogram and for PATH1/PATH2 to drain.
    void FlushPaths();

    // The shared VU1 data memory block (vu_memory.h, qwords 0 to kVuFrameBlockQwords - 1): the
    // caller's constants, the null GIF tag and the output windows reset to window A, plus the VIF1
    // double buffer (BASE, OFFSET), which batches then alternate across programs. Once per frame
    // before the first program, while no program runs.
    void SetFrameConstants(const vu::FrameConstants & constants);

    // Starts any pending batch on the current program, then binds 'program': its MPG chain is
    // CALLed only when it is not in micro memory (vu::Bind). Later batches start at its entry.
    void SetProgram(vu::Program program);

    // A batch whose packet (16-byte aligned, ending with ITOP) is REF'd in place. It starts on the
    // current program from the next batch's tag, whose VIFcodes run before its data reaches the
    // other half, or from EndBatches.
    void DrawBatch(const void * packet, u32 qwords);

    // A batch whose packet rides in the list: 'qwords' (V4-32, STCYCL 1,1) unpacked at the start
    // of the next double buffer half. Returns where to write them; it starts like a DrawBatch.
    qword_t * UnpackBatch(u32 qwords);

    // Starts the pending batch, if any. A chain that closes starts it too.
    void EndBatches()
    {
        StartPendingBatch();
    }

    // ---- Chain ownership ----

    // Start a frame's chain at offset 0 of 'ring'. Any earlier chain must have been closed.
    void Begin(ChainRing & ring);

    // True when commands were written since the last CloseChain.
    bool HasChain() const
    {
        return RingUsedBytes() != m_chainStart;
    }

    // Starts any pending batch, ends the chain with FLUSH, FINISH and END, drains the scratchpad
    // and waits for the fromSPR DMA; returns the chain's first tag for the VIF1 kick. Later
    // commands start a new chain in the same ring.
    const void * CloseChain();

    bool InDirect() const
    {
        return m_directTag != nullptr;
    }

    // Bytes of the ring the list has written or is about to (drained plus pending).
    u32 RingUsedBytes() const
    {
        return m_ringOffset + static_cast<u32>(m_cursor - HalfBase(m_half)) * 16U;
    }

    u32 SprDrains() const
    {
        return m_sprDrains;
    }

    u32 RingRestarts() const
    {
        return m_ringRestarts;
    }

    void ResetCounters()
    {
        m_sprDrains = 0;
        m_ringRestarts = 0;
    }

private:
    // Room for 'qwords' more, the chain's terminator and the pending batch's start; a full ring
    // is kicked, waited for and restarted at offset 0.
    void Ensure(u32 qwords);
    void RestartRing();
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

    // A CNT carrying the pending batch's MSCAL. It needs no reservation: Ensure always leaves room.
    void StartPendingBatch();

    void Drain();
    qword_t * HalfBase(int half) const;

    ChainRing * m_ring = nullptr;
    u32 m_ringOffset = 0; // bytes already drained into the ring
    u32 m_chainStart = 0; // ring offset of the chain the next CloseChain ends
    int m_half = 0;
    qword_t * m_cursor = nullptr;
    u32 m_sprDrains = 0;
    u32 m_ringRestarts = 0;

    qword_t * m_directTag = nullptr;
    qword_t * m_directBase = nullptr;
    int m_directCapacity = 0;

    u32 m_programEntry = kNoProgram; // the bound program's MSCAL address
    u32 m_pendingStart = kVifNop;    // MSCAL of the last batch, not issued yet

    static constexpr u32 kNoProgram = ~0U;
};

} // namespace ps2::renderer::dma
