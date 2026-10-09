// ================================================================================================
// File: audio_tests.h
// Brief: Check portable sample ownership and explicit deferred audio capability failures on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{
bool RunAudioTests();
bool RunAudioFailureProbe(const char * name);
} // namespace ps2::smoketests
