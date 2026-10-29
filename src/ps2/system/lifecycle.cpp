// ================================================================================================
// File: lifecycle.cpp
// Brief: Initialize real core/offline services without presentation, save buffers or worker threads,
// and report the tagged heap at each startup stage.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/lifecycle.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/system/offline_session.h"

#include <idlib/precompiled.h>

namespace ps2::system::lifecycle
{
namespace
{

static constexpr const char * kTagNames[] = {
#define MEM_TAG(x) #x,
#include <idlib/sys/sys_alloc_tags.h>
};
static_assert(ps2::ArrayLength(kTagNames) == TAG_NUM_TAGS);

static Stage s_completed = Stage::None;
static Stage s_stopAfter = Stage::None;
static bool s_everStarted = false;
static bool s_ready = false;

bool Complete(Stage stage)
{
    s_completed = stage;
    Log(LogLevel::Info, "[D3BFG] COMMON stage=%s ready\n", StageName(stage));
    return s_stopAfter == stage;
}

} // namespace

const char * StageName(Stage stage)
{
    switch (stage)
    {
    case Stage::None:
        return "none";
    case Stage::System:
        return "system";
    case Stage::Filesystem:
        return "filesystem";
    case Stage::Idlib:
        return "idlib";
    case Stage::Commands:
        return "commands";
    case Stage::Cvars:
        return "cvars";
    case Stage::Jobs:
        return "jobs";
    case Stage::Session:
        return "session";
    }
    FatalError("invalid Common lifecycle stage");
}

Stage CompletedStage()
{
    return s_completed;
}

void StopAfterForTest(Stage stage)
{
    if (s_everStarted)
    {
        FatalError("Common startup stop must be selected before initialization");
    }
    s_stopAfter = stage;
}

PS2_COLD_FUNC InitResult Init(idCommon & owner)
{
    if (s_everStarted || common != &owner)
    {
        FatalError("invalid/repeated Common initialization");
    }

    s_everStarted = true;
    idLib::sys = sys;
    idLib::common = &owner;
    idLib::cvarSystem = cvarSystem;
    idLib::fileSystem = fileSystem;

    Sys_Init();
    if (Complete(Stage::System))
    {
        return InitResult::Stopped;
    }

    idLib::Init();
    if (Complete(Stage::Idlib))
    {
        return InitResult::Stopped;
    }

    cmdSystem->Init();
    if (Complete(Stage::Commands))
    {
        return InitResult::Stopped;
    }

    cvarSystem->Init();
    idCVar::RegisterStaticVars();
    if (Complete(Stage::Cvars))
    {
        return InitResult::Stopped;
    }

    // The campaign filesystem registers commands and reads cvars during initialization.
    if (!system::fs::Init())
    {
        return InitResult::Failed;
    }

    fileSystem->Init();
    if (!fileSystem->IsInitialized())
    {
        fileSystem->Shutdown(false);
        return InitResult::Failed;
    }
    if (Complete(Stage::Filesystem))
    {
        return InitResult::Stopped;
    }

    parallelJobManager->Init();
    if (Complete(Stage::Jobs))
    {
        return InitResult::Stopped;
    }

    system::session::Init();
    if (Complete(Stage::Session))
    {
        return InitResult::Stopped;
    }

    s_ready = true;
    Log(LogLevel::Info, "[D3BFG] COMMON foundation ready; game/presentation/save capabilities deferred\n");
    return InitResult::Ready;
}

PS2_COLD_FUNC void Shutdown()
{
    s_ready = false;

    // Use the exact reverse dependency order. A service is never shut down before its Init completes.
    while (s_completed != Stage::None)
    {
        const Stage stage = s_completed;
        switch (stage)
        {
        case Stage::Session:
            system::session::Shutdown();
            s_completed = Stage::Jobs;
            break;
        case Stage::Jobs:
            parallelJobManager->Shutdown();
            s_completed = Stage::Filesystem;
            break;
        case Stage::Cvars:
            cvarSystem->Shutdown();
            s_completed = Stage::Commands;
            break;
        case Stage::Commands:
            cmdSystem->Shutdown();
            s_completed = Stage::Idlib;
            break;
        case Stage::Idlib:
            idLib::ShutDown();
            s_completed = Stage::System;
            break;
        case Stage::Filesystem:
            fileSystem->Shutdown(false);
            s_completed = Stage::Cvars;
            break;
        case Stage::System:
            Sys_Shutdown();
            s_completed = Stage::None;
            break;
        case Stage::None:
            break;
        }
        Log(LogLevel::Info, "[D3BFG] COMMON stage=%s shutdown\n", StageName(stage));
    }
}

void Frame()
{
    if (!s_ready || !common->IsInitialized()) [[unlikely]]
    {
        FatalError("Common frame before completed initialization");
    }

    ++idLib::frameNumber;
    cmdSystem->ExecuteCommandBuffer();
    system::session::Frame();
}

PS2_COLD_FUNC void PrintMemory(const char * stage)
{
    const system::heap::Stats totals = system::heap::GetTotalStats();
    const system::heap::ArenaStats arena = system::heap::GetArenaStats();

    Log(LogLevel::Info, "[D3BFG] MEMORY %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu peak_backing=%zu\n",
        stage, totals.requestedBytes, totals.backingBytes, totals.allocationCount, totals.peakRequestedBytes,
        totals.peakBackingBytes);

    Log(LogLevel::Info, "[D3BFG] ARENA %s available=%d committed=%zu used=%zu free=%zu peak_committed=%zu\n", stage,
        arena.available ? 1 : 0, arena.committedBytes, arena.usedBytes, arena.freeBytes, arena.peakCommittedBytes);

    for (std::uint16_t tag = 0; tag < static_cast<std::uint16_t>(TAG_NUM_TAGS); ++tag)
    {
        const system::heap::Stats stats = system::heap::GetStats(tag);

        if (stats.allocationCount != 0 || stats.peakRequestedBytes != 0)
        {
            Log(LogLevel::Info, "[D3BFG] TAG %s %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu\n", stage,
                kTagNames[tag], stats.requestedBytes, stats.backingBytes, stats.allocationCount, stats.peakRequestedBytes);
        }
    }
}

} // namespace ps2::system::lifecycle
