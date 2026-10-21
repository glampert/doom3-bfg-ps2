// ================================================================================================
// File: renderer_gs_tests.h
// Brief: Exercise one native GS scene and release-safe failure paths independently of production.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/system/video_mode.h"

namespace ps2::tests
{

bool RunGsTests(system::video::Standard standard);
bool RunGsFailureProbe(const char * name);

} // namespace ps2::tests
