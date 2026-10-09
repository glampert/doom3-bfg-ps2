// ================================================================================================
// File: deferred_tests.h
// Brief: Check empty multiplayer/save metadata and deferred campaign capability failures on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{
bool RunDeferredTests();
bool RunDeferredFailureProbe(const char * name);
} // namespace ps2::smoketests
