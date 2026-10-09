// ================================================================================================
// File: input_tests.h
// Brief: Check native input action metadata, cleanup and explicit sampling failures on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{
bool RunInputTests();
bool RunInputFailureProbe(const char * name);
} // namespace ps2::smoketests
