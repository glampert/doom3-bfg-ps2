// ================================================================================================
// File: lifecycle.h
// Brief: Track completed Common foundation stages so partial startup has a matching shutdown.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idCommon;

namespace ps2::lifecycle
{

enum class Stage
{
    None,
    System,
    Idlib,
    Commands,
    Cvars,
    Filesystem,
    Jobs,
    Session
};
enum class InitResult
{
    Ready,
    Stopped,
    Failed
};

// Startup is process-wide and one-shot: static CVar registration cannot be repeated after shutdown.
InitResult Init(idCommon & owner);
void Shutdown();
void Frame();
Stage CompletedStage();
const char * StageName(Stage stage);
void StopAfterForTest(Stage stage);

} // namespace ps2::lifecycle
