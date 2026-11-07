// ================================================================================================
// File: logical_renderer.h
// Brief: Doom's logical renderer: id's tr/renderSystem with CPU models, render worlds and a frame stamp.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::renderer
{

// CPU model/world ownership only; no device, images, frame arenas or draw submission.
PS2_COLD_FUNC void InitLogical(const char * defaultMaterialName);
PS2_COLD_FUNC void ShutdownLogical();

void AdvanceLogicalFrame();
bool IsLogicalInitialized();

[[noreturn]] PS2_COLD_FUNC void Unavailable(const char * method);

} // namespace ps2::renderer
