// ================================================================================================
// File: core.h
// Brief: Initialize the retained Doom core behind bounded headless platform services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::core
{

void Init();
void Shutdown();
void PrintMemory(const char * stage);

} // namespace ps2::core
