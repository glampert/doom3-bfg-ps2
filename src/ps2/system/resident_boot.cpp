// ================================================================================================
// File: resident_boot.cpp
// Brief: Supply the resident link entry without claiming game-fixture initialization or gameplay.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <sifrpc.h>

int main()
{
    SifInitRpc(0);
    ps2::FatalError("resident link entry has no game fixture startup; M3 initialization is required");
}
