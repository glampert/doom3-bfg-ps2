// ================================================================================================
// File: boot_stage.h
// Brief: Let source selection supply the core test stage or the SDK-only skip behavior.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::tests
{

void InitTestServices();
const char * RunCoreTestStage(const char * fixturePath);

} // namespace ps2::tests
