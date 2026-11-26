// ================================================================================================
// File: aio.cpp
// Brief: The background loader thread (aio.h), from quake2-ps2's. A slot's ownership moves with its
//        state: the main thread owns a Free, Done or Failed slot, the loader a Queued or Running one.
//        Each side writes a slot's fields and then its state last, behind a compiler barrier, and
//        reads the state first; the threads share one in-order CPU and never run at once, so that is
//        all the ordering the handoff needs. Only the loader reads the drive: it keeps where the
//        descriptor it last read stands, so the chunks of one request, and a request starting where
//        the last one ended, skip their lseek (on USB every call to the IOP's file server costs a
//        SCSI command's latency).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/aio.h"
#include "ps2/system/log.h"

#include <cstdint>
#include <kernel.h>
#include <unistd.h>

extern void * _gp;

namespace ps2::system::aio
{
namespace
{

static constexpr int kSlotCount = 32;
static constexpr int kStackBytes = 16 * 1024;

// The sequence a slot carries in its ticket: a Release moves it on, so a stale ticket never matches.
// 0 is never issued (a default Ticket's).
struct Slot
{
    volatile State state;
    Priority priority;
    u32 sequence;
    int fd;
    u32 offset;
    u32 bytes;
    u8 * dest;
    volatile u32 bytesDone; // loader writes, main reads
    volatile int result;    // loader writes, main reads
};

static Slot s_slots[kSlotCount] = {};
alignas(16) static u8 s_stack[kStackBytes];

static s32 s_threadId = -1;
static s32 s_workSema = -1; // one signal per Submit; the loader serves all it can per wake
static s32 s_doneSema = -1; // one signal per chunk landed; waiters check their slot again
static bool s_threadFailed = false;
static u32 s_nextSequence = 1;

// Loader-only while it runs: where its last read left a descriptor.
static int s_cacheFd = -1;
static u32 s_cachePos = 0;

// Monotonic, loader-written. The cycles are 64-bit: 32 bits of COP0 Count wrap after 14.6 s of reads.
static volatile u32 s_statBytes = 0;
static volatile u32 s_statChunks = 0;
static volatile u32 s_statSeeks = 0;
static volatile u64 s_statIoCycles = 0;

// The compiler may not move a slot's field writes past its state write, nor a field read above the
// state read. The CPU is in order and the threads never overlap, so this is enough.
inline void CompilerBarrier()
{
    asm volatile("" ::: "memory");
}

inline u32 ReadCycles()
{
    u32 cycles;
    asm volatile("mfc0 %0, $9" : "=r"(cycles));
    return cycles;
}

inline bool InFlight(State state)
{
    return state == State::Queued || state == State::Running;
}

inline bool IsCurrent(const Ticket & ticket)
{
    return ticket.slot >= 0 && ticket.slot < kSlotCount && s_slots[ticket.slot].sequence == ticket.sequence &&
           s_slots[ticket.slot].state != State::Free;
}

// ------------------------------------------------------------------------------------------------
// Loader thread
// ------------------------------------------------------------------------------------------------

// The end of the chunk starting at 'position': the next chunk boundary of the file, or the range's end.
static u32 ChunkEnd(u32 position, u32 rangeEnd)
{
    const u32 boundary = (position / kChunkBytes + 1U) * kChunkBytes;
    return boundary < rangeEnd ? boundary : rangeEnd;
}

// Reads [position, position + bytes) of 'fd' into 'dest': 0, or a code. The main thread's ReadNow
// uses it only while the loader idles (no request pending), which owns the cache then.
static int ReadRange(int fd, u32 position, u8 * dest, u32 bytes)
{
    if (fd != s_cacheFd || position != s_cachePos)
    {
        const u32 start = ReadCycles();
        const off_t at = lseek(fd, static_cast<off_t>(position), SEEK_SET);
        s_statIoCycles = s_statIoCycles + (ReadCycles() - start);
        s_statSeeks = s_statSeeks + 1U;

        if (at != static_cast<off_t>(position)) [[unlikely]]
        {
            s_cacheFd = -1;
            return kErrSeek;
        }

        s_cacheFd = fd;
        s_cachePos = position;
    }

    u32 left = bytes;

    while (left != 0)
    {
        const u32 start = ReadCycles();
        const ssize_t read = ::read(fd, dest, left);
        s_statIoCycles = s_statIoCycles + (ReadCycles() - start);
        s_statChunks = s_statChunks + 1U;

        if (read <= 0) [[unlikely]]
        {
            s_cacheFd = -1;
            return kErrRead;
        }

        dest += read;
        left -= static_cast<u32>(read);
        s_cachePos += static_cast<u32>(read);
    }

    return 0;
}

static void Finish(Slot & slot, int result)
{
    slot.result = result;
    CompilerBarrier();
    slot.state = result < 0 ? State::Failed : State::Done;
}

static void RunChunk(Slot & slot)
{
    const u32 done = slot.bytesDone;
    const u32 position = slot.offset + done;
    const u32 bytes = ChunkEnd(position, slot.offset + slot.bytes) - position;
    const int error = ReadRange(slot.fd, position, slot.dest + done, bytes);

    if (error != 0) [[unlikely]]
    {
        Finish(slot, error);
        return;
    }

    slot.bytesDone = done + bytes;
    s_statBytes = s_statBytes + bytes;

    if (slot.bytesDone == slot.bytes)
    {
        Finish(slot, static_cast<int>(slot.bytes));
    }
}

// Claims a picked request: Queued becomes Running, so Cancel can tell a request the loader has not
// touched (dropped at once) from one it reads (waited for). Interrupts are off across the check and
// the store, since the main thread can preempt this one between any two instructions.
static bool Take(Slot & slot)
{
    DIntr();
    const State state = slot.state;

    if (state == State::Queued)
    {
        slot.state = State::Running;
    }

    EIntr();
    return InFlight(state);
}

// The queued or running request to serve next: the lowest priority value, then the oldest.
static Slot * PickBest()
{
    Slot * best = nullptr;

    for (Slot & slot : s_slots)
    {
        if (!InFlight(slot.state))
        {
            continue;
        }

        CompilerBarrier(); // the fields were written before the state

        if (best == nullptr || slot.priority < best->priority || (slot.priority == best->priority && slot.sequence < best->sequence))
        {
            best = &slot;
        }
    }

    return best;
}

static void LoaderLoop(void *)
{
    for (;;)
    {
        WaitSema(s_workSema);

        for (;;)
        {
            Slot * const slot = PickBest();

            if (slot == nullptr)
            {
                break;
            }

            if (!Take(*slot))
            {
                continue; // cancelled between the pick and the take
            }

            RunChunk(*slot);
            SignalSema(s_doneSema);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Main-thread setup
// ------------------------------------------------------------------------------------------------

// Starts the thread on the first request: a run that never streams pays nothing.
PS2_COLD_FUNC static bool EnsureThread()
{
    if (s_threadId >= 0)
    {
        return true;
    }

    if (s_threadFailed)
    {
        return false;
    }

    s_threadFailed = true; // until everything below succeeded

    // One priority below the main thread (a higher number): it runs only while the main thread
    // sleeps, and the main thread takes the CPU back as soon as an interrupt readies it.
    ee_thread_status_t mainStatus = {};

    if (ReferThreadStatus(GetThreadId(), &mainStatus) < 0) [[unlikely]]
    {
        Log(LogLevel::Warning, "aio: the main thread's priority is unknown; reads are synchronous\n");
        return false;
    }

    ee_sema_t sema = {};
    sema.init_count = 0;
    sema.max_count = 255;
    s_workSema = CreateSema(&sema);
    s_doneSema = CreateSema(&sema);

    ee_thread_t thread = {};
    thread.func = reinterpret_cast<void *>(&LoaderLoop);
    thread.stack = s_stack;
    thread.stack_size = kStackBytes;
    thread.gp_reg = &_gp;
    thread.initial_priority = mainStatus.current_priority + 1;

    if (s_workSema >= 0 && s_doneSema >= 0)
    {
        s_threadId = CreateThread(&thread);
    }

    if (s_threadId < 0 || StartThread(s_threadId, nullptr) < 0) [[unlikely]]
    {
        Log(LogLevel::Warning, "aio: the loader thread did not start; reads are synchronous\n");

        if (s_threadId >= 0)
        {
            DeleteThread(s_threadId);
            s_threadId = -1;
        }

        if (s_workSema >= 0)
        {
            DeleteSema(s_workSema);
            s_workSema = -1;
        }

        if (s_doneSema >= 0)
        {
            DeleteSema(s_doneSema);
            s_doneSema = -1;
        }

        return false;
    }

    s_threadFailed = false;
    Log(LogLevel::Info, "[D3BFG] AIO loader thread priority=%d\n", static_cast<int>(thread.initial_priority));
    return true;
}

static void WaitWhileInFlight(const Slot & slot)
{
    // The done semaphore counts every chunk any request landed, so a wake may be another's: check again.
    while (InFlight(slot.state))
    {
        WaitSema(s_doneSema);
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Interface
// ------------------------------------------------------------------------------------------------

bool Enabled()
{
    return !s_threadFailed;
}

Ticket Submit(const Request & request)
{
    PS2_AssertMsg((reinterpret_cast<std::uintptr_t>(request.dest) & 63U) == 0 && request.destBytes >= RoundUp64(request.bytes) &&
                      request.fd >= 0,
                  "aio request with an unaligned or short destination, or no descriptor");

    if (request.bytes == 0 || !EnsureThread())
    {
        return {};
    }

    for (int index = 0; index < kSlotCount; ++index)
    {
        Slot & slot = s_slots[index];

        if (slot.state != State::Free)
        {
            continue;
        }

        slot.priority = request.priority;
        slot.sequence = s_nextSequence++;
        slot.fd = request.fd;
        slot.offset = request.offset;
        slot.bytes = request.bytes;
        slot.dest = static_cast<u8 *>(request.dest);
        slot.bytesDone = 0;
        slot.result = 0;

        if (s_nextSequence == 0)
        {
            s_nextSequence = 1;
        }

        CompilerBarrier();
        slot.state = State::Queued;
        SignalSema(s_workSema);
        return Ticket{ index, slot.sequence };
    }

    return {};
}

State Poll(Ticket ticket)
{
    if (!IsCurrent(ticket))
    {
        return State::Free;
    }

    const State state = s_slots[ticket.slot].state;
    CompilerBarrier();
    return state;
}

int Result(Ticket ticket)
{
    if (!IsCurrent(ticket))
    {
        return kErrNoThread;
    }

    const Slot & slot = s_slots[ticket.slot];
    const State state = slot.state;
    CompilerBarrier();
    return InFlight(state) ? 0 : slot.result;
}

void Wait(Ticket ticket)
{
    if (IsCurrent(ticket))
    {
        WaitWhileInFlight(s_slots[ticket.slot]);
    }
}

void Release(Ticket ticket)
{
    if (!IsCurrent(ticket))
    {
        return;
    }

    Slot & slot = s_slots[ticket.slot];
    PS2_AssertMsg(!InFlight(slot.state), "aio release of a request still in flight");
    CompilerBarrier();
    slot.state = State::Free;
}

void Cancel(Ticket ticket)
{
    if (!IsCurrent(ticket))
    {
        return;
    }

    Slot & slot = s_slots[ticket.slot];

    // Queued to Free is this thread's store alone: the loader claims a request with interrupts off
    // (Take), so it either saw Queued before this and runs it, or sees Free and skips it.
    if (slot.state != State::Queued)
    {
        WaitWhileInFlight(slot);
    }

    CompilerBarrier();
    slot.state = State::Free;
}

void Drain()
{
    if (s_threadId < 0)
    {
        return;
    }

    for (const Slot & slot : s_slots)
    {
        WaitWhileInFlight(slot);
    }

    // Stale chunk signals, so the next wait sleeps rather than running through them.
    while (PollSema(s_doneSema) >= 0)
    {
    }
}

void ForgetDescriptor(int fd)
{
#if PS2_D3BFG_ASSERTS
    for (const Slot & slot : s_slots)
    {
        PS2_AssertMsg(!InFlight(slot.state) || slot.fd != fd, "aio descriptor forgotten under a pending request");
    }
#endif // PS2_D3BFG_ASSERTS

    // The loader idles in WaitSema with nothing pending on 'fd', so the cache is this thread's now.
    if (s_cacheFd == fd)
    {
        s_cacheFd = -1;
    }
}

int ReadNow(const Request & request)
{
#if PS2_D3BFG_ASSERTS
    for (const Slot & slot : s_slots)
    {
        PS2_AssertMsg(!InFlight(slot.state), "aio synchronous read while the loader reads");
    }
#endif // PS2_D3BFG_ASSERTS

    const int error = ReadRange(request.fd, request.offset, static_cast<u8 *>(request.dest), request.bytes);

    if (error == 0)
    {
        s_statBytes = s_statBytes + request.bytes;
    }

    return error != 0 ? error : static_cast<int>(request.bytes);
}

bool Busy()
{
    for (const Slot & slot : s_slots)
    {
        if (InFlight(slot.state))
        {
            return true;
        }
    }

    return false;
}

Stats GetStats()
{
    Stats stats = {};
    stats.bytes = s_statBytes;
    stats.chunks = s_statChunks;
    stats.seeks = s_statSeeks;
    stats.ioCycles = s_statIoCycles;

    for (const Slot & slot : s_slots)
    {
        stats.queueDepth += InFlight(slot.state) ? 1U : 0U;
    }

    return stats;
}

} // namespace ps2::system::aio
