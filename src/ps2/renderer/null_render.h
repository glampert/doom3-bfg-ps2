// ================================================================================================
// File: null_render.h
// Brief: Report unavailable renderer capabilities through the shared fatal sink.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::render
{
[[noreturn]] PS2_COLD_FUNC void Unavailable(const char * method);
} // namespace ps2::render
