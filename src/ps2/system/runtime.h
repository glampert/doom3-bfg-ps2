// ================================================================================================
// File: runtime.h
// Brief: Request an orderly exit at the production loop's frame boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::system::runtime
{
void RequestQuit();
bool IsQuitRequested();
// The package selects headless/ntsc/pal before Common creates any device resources.
void ConfigurePresentation();
void InitPresentation();
void ShutdownPresentation(int frames);
bool HasPresentation();
void BeginPresentationFrame();
void PresentConsoleFrame(int frames);
void UpdateScreen(bool captureToImage);
void LogPresentationStatus(int frames);
} // namespace ps2::system::runtime
