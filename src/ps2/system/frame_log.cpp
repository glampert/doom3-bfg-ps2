// ================================================================================================
// File: frame_log.cpp
// Brief: The frame log (frame_log.h): a row per Common frame from COP0 Count stamps and the
//        renderer's and loader's cumulative counters, kept in EE memory and written once.
//
//  Times are the EE's: a frame runs its game step, builds its views and screens, then its kick waits
//  for the chain before it (gpu: VU1 and the GS, all of the frame's own chain with serial frames)
//  and for the vertical blank (vsync) to flip it. The gpu column counts every wait of the frame for
//  a chain, mid-frame ones (a full ring, a texture fence) included, and with the GPU sampler splits
//  into what held the chain up (dma::GpuState: vu, gs, direct, vif, tail); io is the time the
//  loader thread spent inside reads and seeks during the frame, which it gets while the main thread
//  sleeps, so it overlaps the vsync wait.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/frame_log.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/renderer/scene.h"
#include "ps2/renderer/world.h"
#include "ps2/renderer/world_textures.h"
#include "ps2/system/aio.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include "ps2/system/video_mode.h"

#include <idlib/precompiled.h> // TAG_DEBUG, idStr's formatting

#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

// The header refuses direct fileXio use beside the C library unless told the caller knows: the
// file goes through the C library, and only the PFS sync is a direct call.
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#if PS2_D3BFG_FRAME_LOG

namespace ps2::system::frame_log
{

namespace
{

#if PS2_D3BFG_GPU_SAMPLER
static constexpr int kGpuStates = static_cast<int>(renderer::dma::GpuState::Count);
static_assert(kGpuStates == 5, "the frame log writes the GPU sampler's states as vu, gs, direct, vif and tail");
#endif // PS2_D3BFG_GPU_SAMPLER

// What the counters read at a frame boundary: the renderer's and the loader's cumulative totals.
struct Counters
{
    u32 cycles = 0;
    u64 gpuCycles = 0;
#if PS2_D3BFG_GPU_SAMPLER
    u64 gpuStates[kGpuStates] = {};
    renderer::dma::ListPathStats listPath = {};
#endif // PS2_D3BFG_GPU_SAMPLER
    u64 vsyncCycles = 0;
    u64 ioCycles = 0;
    u32 presents = 0;
    u32 vsyncs = 0;
    u32 kicks = 0;
    u32 drains = 0;
    renderer::world::DrawStats draw = {};
    u32 loaderBytes = 0;
    u32 chunks = 0;
    u32 seeks = 0;
    // The last pass's texture totals, cumulative since its map loaded.
    u32 uploads = 0;
    u32 uploadBytes = 0;
    u32 evictions = 0;
    u32 passEvictions = 0;
    u32 streamLoads = 0;
    u32 streamBytes = 0;
    u32 streamMisses = 0;
    u32 fallbacks = 0;
    u32 passFallbacks = 0;
    u32 streamWaitMs = 0;
};

// One frame. Times stay in EE cycles until the file is written (one frame is far below Count's
// 14.6 s wrap); the rest are the frame's share of each counter, or the state at its end.
struct Row
{
    u32 frameCycles;
    u32 gameCycles;
    u32 viewCycles;
    u32 uiCycles;
    u32 submitCycles;
    u32 gpuCycles;
#if PS2_D3BFG_GPU_SAMPLER
    u32 gpuStates[kGpuStates];
    // The chains' list path (dma::ListPathStats): batches, stream entries, clipper calls, and
    // the calls that sent nothing.
    u32 listBatches;
    u32 listEntries;
    u32 clips;
    u32 emptyClips;
#endif // PS2_D3BFG_GPU_SAMPLER
    u32 vsyncCycles;
    u32 ioCycles;
    u32 streamWaitMs;
    s32 area;
    u32 areas;
    u32 entities;
    u32 batches;
    u32 litBatches;
    u32 passBatches;
    u32 vertices;
    u32 packetQwords;
    u32 chainBytes;
    u32 kicks;
    u32 drains;
    u32 presents;
    u32 vsyncs;
    u32 uploads;
    u32 uploadBytes;
    u32 evictions;
    u32 passEvictions;
    u32 streamLoads;
    u32 streamBytes;
    u32 streamMisses;
    u32 fallbacks;
    u32 passFallbacks;
    u32 loaderBytes;
    u32 chunks;
    u32 seeks;
    u32 queue;
    u32 copyBytes;
    u32 residentBytes;
    s32 eye[3];
    s32 yaw;
};

static Row * s_rows = nullptr;
static u32 s_capacity = 0;
static u32 s_count = 0;
static bool s_recording = false;
// The first boundary after Start opens the first row; there is no frame before it to record.
static bool s_open = false;
static Counters s_start = {};
static Counters s_first = {};
static u32 s_marks[static_cast<int>(Point::Count)] = {};
static float s_eye[3] = {};
static float s_yaw = 0.0f;
static char s_label[160] = {};
static char s_fileName[64] = {};

// A counter's growth since 'before', or its value when it started again from zero (a texture
// total after a map load).
u32 Grown(u32 now, u32 before)
{
    return now >= before ? now - before : now;
}

Counters Read()
{
    Counters counters;
    counters.cycles = renderer::gs::ReadCycles();

    const renderer::dma::Stats & dma = renderer::dma::GetStats();
    counters.gpuCycles = dma.waitCycles;

#if PS2_D3BFG_GPU_SAMPLER
    const renderer::dma::GpuStats & gpu = renderer::dma::GetGpuStats();

    for (int state = 0; state < kGpuStates; ++state)
    {
        counters.gpuStates[state] = gpu.cycles[state];
    }

    counters.listPath = gpu.listPath;
#endif // PS2_D3BFG_GPU_SAMPLER

    counters.kicks = dma.kicks;
    counters.drains = dma.drains;

    const renderer::gs::PresentStats & present = renderer::gs::GetPresentStats();
    counters.vsyncCycles = present.vsyncCycles;
    counters.presents = present.presents;
    counters.vsyncs = present.vsyncs;

    counters.draw = renderer::world::GetDrawStats();

    const aio::Stats loader = aio::GetStats();
    counters.ioCycles = loader.ioCycles;
    counters.loaderBytes = loader.bytes;
    counters.chunks = loader.chunks;
    counters.seeks = loader.seeks;

    const renderer::world::TextureStats & textures = renderer::world::LastPassTextureStats();
    counters.uploads = textures.uploads;
    counters.uploadBytes = textures.uploadBytes;
    counters.evictions = textures.evictions;
    counters.passEvictions = textures.passEvictions;
    counters.streamLoads = textures.streamLoads;
    counters.streamBytes = textures.streamBytes;
    counters.streamMisses = textures.streamMisses;
    counters.fallbacks = textures.untexturedFallbacks;
    counters.passFallbacks = textures.passFallbacks;
    counters.streamWaitMs = textures.streamWaitMs;
    return counters;
}

// The span between two marks of the frame, zero when the frame never reached either.
u32 Span(u32 from, u32 to)
{
    return from != 0 && to != 0 ? to - from : 0;
}

void Capture(const Counters & before, const Counters & now)
{
    Row & row = s_rows[s_count++];
    const u32 * marks = s_marks;

    row.frameCycles = now.cycles - before.cycles;
    row.gameCycles = Span(before.cycles, marks[static_cast<int>(Point::Present)]);
    row.viewCycles = Span(marks[static_cast<int>(Point::Present)], marks[static_cast<int>(Point::View)]);
    row.uiCycles = Span(marks[static_cast<int>(Point::View)], marks[static_cast<int>(Point::Ui)]);
    row.submitCycles = Span(marks[static_cast<int>(Point::Ui)], marks[static_cast<int>(Point::Presented)]);
    row.gpuCycles = static_cast<u32>(now.gpuCycles - before.gpuCycles);

#if PS2_D3BFG_GPU_SAMPLER
    for (int state = 0; state < kGpuStates; ++state)
    {
        row.gpuStates[state] = static_cast<u32>(now.gpuStates[state] - before.gpuStates[state]);
    }

    row.listBatches = static_cast<u32>(now.listPath.batches - before.listPath.batches);
    row.listEntries = static_cast<u32>(now.listPath.entries - before.listPath.entries);
    row.clips = static_cast<u32>(now.listPath.clips - before.listPath.clips);
    row.emptyClips = static_cast<u32>(now.listPath.empty - before.listPath.empty);
#endif // PS2_D3BFG_GPU_SAMPLER

    row.vsyncCycles = static_cast<u32>(now.vsyncCycles - before.vsyncCycles);
    row.ioCycles = static_cast<u32>(now.ioCycles - before.ioCycles);
    row.streamWaitMs = Grown(now.streamWaitMs, before.streamWaitMs);

    const renderer::scene::Stats & scene = renderer::scene::GetStats();
    row.area = scene.area;
    row.areas = scene.areas;
    row.entities = scene.entities;

    row.batches = now.draw.batches - before.draw.batches;
    row.litBatches = now.draw.litBatches - before.draw.litBatches;
    row.passBatches = now.draw.passBatches - before.draw.passBatches;
    row.vertices = now.draw.vertices - before.draw.vertices;
    row.packetQwords = now.draw.packetQwords - before.draw.packetQwords;
    row.chainBytes = renderer::dma::GetStats().frameBytes;
    row.kicks = now.kicks - before.kicks;
    row.drains = Grown(now.drains, before.drains);
    row.presents = now.presents - before.presents;
    row.vsyncs = now.vsyncs - before.vsyncs;

    row.uploads = Grown(now.uploads, before.uploads);
    row.uploadBytes = Grown(now.uploadBytes, before.uploadBytes);
    row.evictions = Grown(now.evictions, before.evictions);
    row.passEvictions = Grown(now.passEvictions, before.passEvictions);
    row.streamLoads = Grown(now.streamLoads, before.streamLoads);
    row.streamBytes = Grown(now.streamBytes, before.streamBytes);
    row.streamMisses = Grown(now.streamMisses, before.streamMisses);
    row.fallbacks = Grown(now.fallbacks, before.fallbacks);
    row.passFallbacks = Grown(now.passFallbacks, before.passFallbacks);

    row.loaderBytes = now.loaderBytes - before.loaderBytes;
    row.chunks = now.chunks - before.chunks;
    row.seeks = now.seeks - before.seeks;
    row.queue = aio::GetStats().queueDepth;

    const renderer::world::TextureStats & textures = renderer::world::LastPassTextureStats();
    row.copyBytes = textures.copyBytes;
    row.residentBytes = textures.residentBytes;

    for (int lane = 0; lane < 3; ++lane)
    {
        row.eye[lane] = static_cast<s32>(s_eye[lane]);
    }

    row.yaw = static_cast<s32>(s_yaw);
}

PS2_COLD_FUNC u32 Microseconds(u64 cycles)
{
    return static_cast<u32>(cycles * 1000000U / aio::kCyclesPerSecond);
}

// The file: built in a TAG_DEBUG buffer and written whenever it fills, through one descriptor.
class Writer
{
public:
    PS2_COLD_FUNC bool Open(const char * path)
    {
        m_buffer = static_cast<char *>(heap::TryAlloc(kBufferBytes, TAG_DEBUG));
        m_fd = m_buffer != nullptr ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
        m_failed = m_fd < 0;
        m_used = 0;
        m_bytes = 0;
        return !m_failed;
    }

    PS2_COLD_FUNC void Line(const char * format, ...) PS2_PRINTF_FUNC(2, 3);

    // Closes the file; a package on the hard disk syncs its metadata too (PFS keeps it cached past
    // the close).
    PS2_COLD_FUNC bool Close()
    {
        Flush();
        heap::Free(m_buffer);
        m_buffer = nullptr;

        if (m_fd < 0)
        {
            return false;
        }

        // Through fileXio a successful close returns the descriptor's slot: only -1 fails.
        const bool closed = close(m_fd) >= 0;
        m_fd = -1;
        const char * root = runtime::PackageRoot();
        const char * colon = strchr(root, ':');
        bool synced = true;

        if (idStr::Cmpn(root, "pfs", 3) == 0 && colon != nullptr && colon - root + 1 < 16)
        {
            char device[16] = {};
            memcpy(device, root, static_cast<size_t>(colon - root + 1));
            synced = fileXioSync(device, FXIO_WAIT) == 0;
        }

        return closed && synced && !m_failed;
    }

    u32 Bytes() const
    {
        return m_bytes;
    }

private:
    static constexpr int kBufferBytes = 16 * 1024;
    static constexpr int kLineBytes = 512;

    PS2_COLD_FUNC void Flush()
    {
        if (m_used > 0 && !m_failed && write(m_fd, m_buffer, static_cast<size_t>(m_used)) != m_used)
        {
            m_failed = true;
        }

        m_used = 0;
    }

    char * m_buffer = nullptr;
    int m_fd = -1;
    bool m_failed = true;
    int m_used = 0;
    u32 m_bytes = 0;
};

PS2_COLD_FUNC void Writer::Line(const char * format, ...)
{
    if (m_failed)
    {
        return;
    }

    if (m_used > kBufferBytes - kLineBytes)
    {
        Flush();
    }

    va_list args;
    va_start(args, format);
    const int length = idStr::vsnPrintf(m_buffer + m_used, kLineBytes, format, args);
    va_end(args);

    if (length < 0 || length >= kLineBytes) [[unlikely]]
    {
        m_failed = true;
        return;
    }

    m_used += length;
    m_bytes += static_cast<u32>(length);
}

static Writer s_writer;

PS2_COLD_FUNC void WriteRows(Writer & writer, const Counters & last)
{
    writer.Line("FLOG#note,doom3-bfg-ps2 frame log; times in microseconds, sizes in bytes\n");
    writer.Line("FLOG#build,%s\n", PS2_D3BFG_DEBUG ? "debug" : "release");
    writer.Line("FLOG#package,%s\n", runtime::PackageRoot());
    writer.Line("FLOG#display,%dx%d,%s,%s\n", renderer::gs::Width(), renderer::gs::Height(),
                system::video::InterlaceName(renderer::gs::GetInterlace()), renderer::gs::FrameFormatLabel(renderer::gs::GetFrameFormat()));
    writer.Line("FLOG#label,%s\n", s_label);

    // The GPU sampler's columns split gpu_us, then count the chains' list path; a log built without
    // it has none of them.
#if PS2_D3BFG_GPU_SAMPLER
    static constexpr const char * kGpuColumns = "vu_us,gs_us,direct_us,vif_us,tail_us,list_batches,list_entries,clips,empty_clips,";
#else // PS2_D3BFG_GPU_SAMPLER
    static constexpr const char * kGpuColumns = "";
#endif // PS2_D3BFG_GPU_SAMPLER

    writer.Line("FLOG#hdr,frame,frame_us,game_us,view_us,ui_us,submit_us,gpu_us,%svsync_us,io_us,stream_wait_ms,area,areas,entities,"
                "batches,lit_batches,pass_batches,vertices,packet_bytes,chain_bytes,kicks,drains,presents,vsyncs,uploads,upload_bytes,"
                "evictions,pass_evictions,stream_loads,stream_bytes,stream_misses,fallbacks,pass_fallbacks,loader_bytes,chunks,seeks,"
                "queue,copy_bytes,resident_bytes,x,y,z,yaw\n",
                kGpuColumns);

    for (u32 index = 0; index < s_count; ++index)
    {
        const Row & row = s_rows[index];
        char gpu[128] = {};

#if PS2_D3BFG_GPU_SAMPLER
        idStr::snPrintf(gpu, sizeof(gpu), "%u,%u,%u,%u,%u,%u,%u,%u,%u,", Microseconds(row.gpuStates[0]), Microseconds(row.gpuStates[1]),
                        Microseconds(row.gpuStates[2]), Microseconds(row.gpuStates[3]), Microseconds(row.gpuStates[4]), row.listBatches,
                        row.listEntries, row.clips, row.emptyClips);
#endif // PS2_D3BFG_GPU_SAMPLER

        writer.Line("FLOG,%u,%u,%u,%u,%u,%u,%u,%s%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                    "%u,%d,%d,%d,%d\n",
                    index, Microseconds(row.frameCycles), Microseconds(row.gameCycles), Microseconds(row.viewCycles),
                    Microseconds(row.uiCycles), Microseconds(row.submitCycles), Microseconds(row.gpuCycles), gpu, Microseconds(row.vsyncCycles),
                    Microseconds(row.ioCycles), row.streamWaitMs, static_cast<int>(row.area), row.areas, row.entities, row.batches,
                    row.litBatches, row.passBatches, row.vertices, row.packetQwords * 16U, row.chainBytes, row.kicks, row.drains,
                    row.presents, row.vsyncs, row.uploads, row.uploadBytes, row.evictions, row.passEvictions, row.streamLoads,
                    row.streamBytes, row.streamMisses, row.fallbacks, row.passFallbacks, row.loaderBytes, row.chunks, row.seeks,
                    row.queue, row.copyBytes, row.residentBytes, static_cast<int>(row.eye[0]), static_cast<int>(row.eye[1]),
                    static_cast<int>(row.eye[2]), static_cast<int>(row.yaw));
    }

    // The loader over the recorded frames: its read rate is bytes over the time inside the reads.
    const u64 io = last.ioCycles - s_first.ioCycles;
    const u32 bytes = last.loaderBytes - s_first.loaderBytes;
    const u32 ioUs = Microseconds(io);
    writer.Line("FLOG#loader,bytes=%u,chunks=%u,seeks=%u,io_us=%u,kib_per_s=%u\n", bytes, last.chunks - s_first.chunks,
                last.seeks - s_first.seeks, ioUs, ioUs != 0 ? static_cast<u32>(static_cast<u64>(bytes) * 1000000U / 1024U / ioUs) : 0U);
    writer.Line("FLOG#end,%u\n", s_count);
}

} // namespace

PS2_COLD_FUNC bool Start(u32 frames, const char * label, const char * fileName)
{
    if (s_rows != nullptr || frames == 0 || frames > kMaxFrames) [[unlikely]]
    {
        Log(LogLevel::Error, "[D3BFG] FRAMELOG cannot start: %s\n", s_rows != nullptr ? "a log records already" : "frames must be 1 to 16384");
        return false;
    }

    s_rows = static_cast<Row *>(heap::TryAlloc(frames * sizeof(Row), TAG_DEBUG));

    if (s_rows == nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "[D3BFG] FRAMELOG cannot start: no memory for %u rows\n", frames);
        return false;
    }

    s_capacity = frames;
    s_count = 0;
    s_open = false;
    s_recording = true;
    idStr::snPrintf(s_label, sizeof(s_label), "%s", label != nullptr ? label : "");
    idStr::snPrintf(s_fileName, sizeof(s_fileName), "%s", fileName != nullptr ? fileName : kDefaultFileName);
    Log(LogLevel::Info, "[D3BFG] FRAMELOG started frames=%u bytes=%u file=%s label=%s\n", frames, static_cast<u32>(frames * sizeof(Row)),
        s_fileName, s_label);
    return true;
}

PS2_COLD_FUNC bool Stop()
{
    if (s_rows == nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "[D3BFG] FRAMELOG cannot stop: no log records\n");
        return false;
    }

    const Counters last = Read();
    s_recording = false;
    char path[256];
    runtime::PackageFile(path, sizeof(path), s_fileName);

    bool written = s_count > 0 && s_writer.Open(path);

    if (written)
    {
        WriteRows(s_writer, last);
    }

    written = s_writer.Close() && written;

    u64 frameCycles = 0;
    u32 worst = 0;

    for (u32 index = 0; index < s_count; ++index)
    {
        frameCycles += s_rows[index].frameCycles;
        worst = s_rows[index].frameCycles > worst ? s_rows[index].frameCycles : worst;
    }

    Log(written ? LogLevel::Info : LogLevel::Error, "[D3BFG] FRAMELOG %s path=%s rows=%u bytes=%u frame_us_mean=%u frame_us_max=%u\n",
        written ? "written" : "write failed", path, s_count, s_writer.Bytes(),
        s_count != 0 ? Microseconds(frameCycles / s_count) : 0U, Microseconds(worst));

    heap::Free(s_rows);
    s_rows = nullptr;
    s_capacity = 0;
    s_count = 0;
    return written;
}

bool Recording()
{
    return s_recording;
}

const char * FileName()
{
    return s_fileName;
}

void BeginFrame()
{
    if (!s_recording)
    {
        return;
    }

    const Counters now = Read();

    if (s_open)
    {
        // A full log ends at the first frame it has no row for.
        if (s_count == s_capacity) [[unlikely]]
        {
            Stop();
            return;
        }

        Capture(s_start, now);
    }
    else
    {
        s_first = now;
        s_open = true;
    }

    s_start = now;
    memset(s_marks, 0, sizeof(s_marks));
}

void Mark(Point point)
{
    if (s_recording)
    {
        // A zero stamp reads as an unreached mark; Count passing exactly 0 costs one cycle.
        const u32 cycles = renderer::gs::ReadCycles();
        s_marks[static_cast<int>(point)] = cycles != 0 ? cycles : 1U;
    }
}

void SetView(float x, float y, float z, float yaw)
{
    s_eye[0] = x;
    s_eye[1] = y;
    s_eye[2] = z;
    s_yaw = yaw;
}

PS2_COLD_FUNC void LogStatus()
{
    Log(LogLevel::Info, "[D3BFG] FRAMELOG status recording=%d rows=%u capacity=%u\n", s_recording ? 1 : 0, s_count, s_capacity);
}

} // namespace ps2::system::frame_log

#endif // PS2_D3BFG_FRAME_LOG
