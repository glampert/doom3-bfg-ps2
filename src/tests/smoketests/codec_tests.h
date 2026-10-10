// ================================================================================================
// File: codec_tests.h
// Brief: Check bounded JPEG and streaming compression on the target with exact cleanup ledgers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{
bool RunCodecTests();
bool RunCodecFailureProbe(const char * name);
} // namespace ps2::smoketests
