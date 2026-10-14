// ================================================================================================
// File: ui_draw.h
// Brief: Activate native screen-space console drawing independently of logical worlds and OpenGL.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::ui
{
// Device ownership remains with rs; initialize after it, shut down before it.
bool Init();
void Shutdown();
bool IsInitialized();
} // namespace ps2::ui
