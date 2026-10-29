// ================================================================================================
// File: runtime.h
// Brief: Request an orderly exit at the production loop's frame boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::system::runtime
{

void RequestQuit();
bool IsQuitRequested();

// The package selects headless/ntsc/pal before Common creates any device resources.
PS2_COLD_FUNC void ConfigurePresentation();
void InitPresentation();
void ShutdownPresentation(int frames);
bool HasPresentation();
void BeginPresentationFrame();
void PresentConsoleFrame(int frames);
void UpdateScreen(bool captureToImage);
void LogPresentationStatus(int frames);

// The visible presentation registers its device poll; Sys_GenerateEvents calls it each frame.
void SetEventGenerator(void (*generator)());

} // namespace ps2::system::runtime
