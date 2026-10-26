// ================================================================================================
// File: input_tests.cpp
// Brief: Verify the native action table and disabled input policy without fabricating player commands.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/input_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// The native user-command header relies on complete engine declarations.
#include <framework/UsercmdGen.h>

namespace ps2::tests
{
namespace
{
bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK input/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}
} // namespace

bool RunInputTests()
{
    bool metadata = usercmdGen != nullptr && userCmdStrings[UB_MAX_BUTTONS - 1].string == nullptr &&
                    userCmdStrings[UB_MAX_BUTTONS - 1].button == UB_NONE;
    for (int index = 0; index < UB_MAX_BUTTONS - 1; ++index)
    {
        const userCmdString_t & entry = userCmdStrings[index];
        metadata = metadata && entry.string != nullptr && entry.button == index + 1 &&
                   usercmdGen->CommandStringUsercmdData(entry.string) == entry.button;
    }
    // Check native names independently of the exported table, including common prefix collisions.
    metadata = metadata && usercmdGen->CommandStringUsercmdData("_MOVEUP") == UB_MOVEUP &&
               usercmdGen->CommandStringUsercmdData("_forward") == UB_MOVEFORWARD &&
               usercmdGen->CommandStringUsercmdData("_attack") == UB_ATTACK &&
               usercmdGen->CommandStringUsercmdData("_use") == UB_USE &&
               usercmdGen->CommandStringUsercmdData("_impulse1") == UB_IMPULSE1 &&
               usercmdGen->CommandStringUsercmdData("_impulse10") == UB_IMPULSE10 &&
               usercmdGen->CommandStringUsercmdData("_IMPULSE31") == UB_IMPULSE31 &&
               usercmdGen->CommandStringUsercmdData("_impulse32") == UB_NONE &&
               usercmdGen->CommandStringUsercmdData("_attack extra") == UB_NONE &&
               usercmdGen->CommandStringUsercmdData("say fixture") == UB_NONE &&
               usercmdGen->CommandStringUsercmdData("") == UB_NONE;
    bool passed = Check("native-action-table", metadata);

    const system::heap::Stats before = system::heap::GetTotalStats();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        usercmdGen->Clear();
        usercmdGen->ClearAngles();
        usercmdGen->Shutdown();
        usercmdGen->Shutdown();
    }
    const system::heap::Stats after = system::heap::GetTotalStats();
    passed = Check("inactive-cleanup-ledger", usercmdGen->ButtonState(-1) == -1 &&
                                              usercmdGen->ButtonState(UB_MAX_BUTTONS) == -1 &&
                                              usercmdGen->KeyState(-1) == -1 && usercmdGen->KeyState(K_LAST_KEY) == -1 &&
                                              after.requestedBytes == before.requestedBytes &&
                                              after.backingBytes == before.backingBytes &&
                                              after.allocationCount == before.allocationCount) &&
             passed;

    const idCVar * joystick = cvarSystem->Find("in_useJoystick");
    const idCVar * rumble = cvarSystem->Find("in_joystickRumble");
    cmdSystem->BufferCommandText(CMD_EXEC_NOW, "in_useJoystick 1; in_joystickRumble 1\n");
    passed = Check("disabled-controller-policy", joystick != nullptr && rumble != nullptr &&
                                                 !joystick->GetBool() && !rumble->GetBool() &&
                                                 (joystick->GetFlags() & (CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM)) == (CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM) &&
                                                 (rumble->GetFlags() & (CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM)) == (CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM)) &&
             passed;
    return passed;
}

} // namespace ps2::tests
