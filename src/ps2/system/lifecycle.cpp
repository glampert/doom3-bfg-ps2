// ================================================================================================
// File: lifecycle.cpp
// Brief: Initialize real core/offline services without presentation, save buffers or worker threads.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/lifecycle.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"
#include "ps2/system/offline_session.h"

#include <idlib/precompiled.h>

namespace ps2::lifecycle
{
namespace
{

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

Stage CompletedStage() { return s_completed; }

void StopAfterForTest(Stage stage)
{
    if (s_everStarted)
    {
        FatalError("Common startup stop must be selected before initialization");
    }
    s_stopAfter = stage;
}

InitResult Init(idCommon & owner)
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
    if (!filesystem::Init())
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
    offline::Init();
    if (Complete(Stage::Session))
    {
        return InitResult::Stopped;
    }
    s_ready = true;
    Log(LogLevel::Info, "[D3BFG] COMMON foundation ready; game/presentation/save capabilities deferred\n");
    return InitResult::Ready;
}

void Shutdown()
{
    s_ready = false;
    // Use the exact reverse dependency order. A service is never shut down before its Init completes.
    while (s_completed != Stage::None)
    {
        const Stage stage = s_completed;
        switch (stage)
        {
        case Stage::Session:
            offline::Shutdown();
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
    if (!s_ready || !common->IsInitialized())
    {
        FatalError("Common frame before completed initialization");
    }
    ++idLib::frameNumber;
    cmdSystem->ExecuteCommandBuffer();
    offline::Frame();
}

} // namespace ps2::lifecycle
