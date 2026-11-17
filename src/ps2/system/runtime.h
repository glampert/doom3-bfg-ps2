// ================================================================================================
// File: runtime.h
// Brief: Request an orderly exit at the production loop's frame boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

#include <cstddef>

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

// The package's root, where base/, irx/, presentation.cfg and console.txt sit: "host:" until
// production's main sets the one the IOP boot found (iop_boot.h), and for every test image.
PS2_COLD_FUNC void SetPackageRoot(const char * root);
const char * PackageRoot();

// "<root>/<name>" for a file at the package's root. Fatal when it would not fit: a load-time path.
PS2_COLD_FUNC void PackageFile(char * output, size_t capacity, const char * name);

} // namespace ps2::system::runtime
