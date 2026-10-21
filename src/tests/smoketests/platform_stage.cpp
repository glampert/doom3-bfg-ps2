// ================================================================================================
// File: platform_stage.cpp
// Brief: Keep the SDK-only probe independent of Doom core services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/boot_stage.h"

namespace ps2::tests
{

void InitTestServices()
{
}

const char * RunCoreTestStage(const char *)
{
    return "SKIP";
}

} // namespace ps2::tests
