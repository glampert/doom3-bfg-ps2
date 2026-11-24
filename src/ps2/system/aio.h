// ================================================================================================
// File: aio.h
// Brief: The background loader: one thread that reads file ranges into buffers the main thread
//        owns, so a texture streamed in mid-level never holds a frame on the drive. Every file read
//        is a SIF RPC that sleeps its caller until the IOP's file server answers (quake2-ps2 measured
//        41 ms for a 32 KB chunk on a USB stick, under 10 ms on the HDD); here the main thread hands
//        the range to this thread, which runs one priority below it, so it never preempts frame work
//        and gets the CPU whenever the main thread sleeps (the vsync wait, gs.cpp). The kernel does
//        not time-slice.
//
//        Requests run a chunk at a time (kChunkBytes on the file's chunk boundaries), and the best
//        (priority, sequence) request is picked again after every chunk.
//
//        Rules. The loader touches its request slots, the descriptors it is handed (it never opens or
//        closes one), lseek and read. It never allocates, logs or touches the engine: a failure is a
//        code in the request. While a request is pending the main thread must not free or write its
//        destination or close its descriptor (Cancel or Drain first, then ForgetDescriptor). A
//        destination is a SIF DMA target: 64-byte aligned and sized in whole 64-byte units.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <tamtypes.h>

namespace ps2::system::aio
{

// One read request to the drive: 32 KB clusters and fileXio's 64 KB IOP buffer take it whole when it
// starts on a 32 KB boundary of the file.
static constexpr u32 kChunkBytes = 32U * 1024U;

static constexpr u32 RoundUp64(u32 bytes)
{
    return (bytes + 63U) & ~63U;
}

// Lower values are served first, picked again at every chunk boundary.
enum class Priority : u8
{
    High,
    Low
};

enum class State : u8
{
    Free,    // Nobody's: what Poll answers for a stale or invalid ticket.
    Queued,  // Submitted, not started.
    Running, // Some chunks landed.
    Done,    // All of it landed.
    Failed   // Result() is a negative code; the destination holds nothing usable.
};

// Result() codes.
static constexpr int kErrSeek = -1;     // lseek to the range failed
static constexpr int kErrRead = -2;     // read failed or ended early
static constexpr int kErrNoThread = -3; // a stale ticket

struct Request
{
    Priority priority = Priority::Low;
    int fd = -1;
    u32 offset = 0;
    u32 bytes = 0;
    void * dest = nullptr; // 64-byte aligned, at least RoundUp64(bytes)
    u32 destBytes = 0;
};

// What Submit hands back. A default one is invalid; one of a released slot goes stale, and Poll
// answers Free for it.
struct Ticket
{
    int slot = -1;
    u32 sequence = 0;

    bool IsValid() const
    {
        return slot >= 0;
    }
};

// Queues a request. Invalid when the thread could not start or every slot is taken: the client then
// reads synchronously (ReadNow) or tries again later.
Ticket Submit(const Request & request);

State Poll(Ticket ticket);

// Bytes delivered, or a negative code, once Poll says Done or Failed.
int Result(Ticket ticket);

// Sleeps until the request is Done or Failed; the loader runs meanwhile.
void Wait(Ticket ticket);

// Gives a Done or Failed request's slot back.
void Release(Ticket ticket);

// Takes the request back whatever its state: a queued one is dropped at once, a running one waited
// for (one chunk at most), a finished one released. The destination is the caller's again.
void Cancel(Ticket ticket);

// Waits until no request is queued or running.
void Drain();

// Before closing a descriptor the loader read: no request on it may be pending. Its next user then
// seeks first, even when the number comes back for another file.
void ForgetDescriptor(int fd);

// Reads the range on the calling thread (lseek and read): Submit's fallback when the loader is off.
// Bytes delivered, or a negative code. No request may be pending (Drain first).
int ReadNow(const Request & request);

// False when the thread could not start: every client reads synchronously.
bool Enabled();

// Cumulative since boot.
struct Stats
{
    u32 bytes;      // delivered into destinations
    u32 chunks;     // read calls made
    u32 ioCycles;   // EE cycles spent inside them (COP0 Count, 294.912 MHz)
    u32 queueDepth; // requests queued or running now
};

Stats GetStats();

} // namespace ps2::system::aio
