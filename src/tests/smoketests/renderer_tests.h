// ================================================================================================
// File: renderer_tests.h
// Brief: Check inactive native renderer metadata and explicit capability failures on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{
bool RunRendererTests();
bool RunRendererFailureProbe(const char * name);
} // namespace ps2::smoketests
