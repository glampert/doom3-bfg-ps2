// ================================================================================================
// File: offline_tests.h
// Brief: Verify campaign session transitions and Common lifecycle boundaries on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::smoketests
{

bool RunOfflineTests();
bool IsCommonProbe(const char * path);
bool RunCommonProbe(const char * path);

} // namespace ps2::smoketests
