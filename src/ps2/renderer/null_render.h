// ================================================================================================
// File: null_render.h
// Brief: Select CPU world ownership and report unavailable renderer output capabilities.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::renderer
{
// CPU model/world ownership only; no device, images, frame arenas or draw submission.
void InitLogical(const char * defaultMaterialName);
void ShutdownLogical();
void AdvanceLogicalFrame();
bool IsLogicalInitialized();
[[noreturn]] PS2_COLD_FUNC void Unavailable(const char * method);
} // namespace ps2::renderer
