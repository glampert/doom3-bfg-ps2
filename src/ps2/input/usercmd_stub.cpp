// ================================================================================================
// File: usercmd_stub.cpp
// Brief: Preserve native input action metadata and reject sampling until an input source exists.
// Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// The native user-command header relies on complete engine declarations.
#include <framework/UsercmdGen.h>

// Retained SWF code indexes this native table in reverse, including its final sentinel.
userCmdString_t userCmdStrings[] = {
    { "_moveUp", UB_MOVEUP },
    { "_moveDown", UB_MOVEDOWN },
    { "_left", UB_LOOKLEFT },
    { "_right", UB_LOOKRIGHT },
    { "_forward", UB_MOVEFORWARD },
    { "_back", UB_MOVEBACK },
    { "_lookUp", UB_LOOKUP },
    { "_lookDown", UB_LOOKDOWN },
    { "_moveLeft", UB_MOVELEFT },
    { "_moveRight", UB_MOVERIGHT },

    { "_attack", UB_ATTACK },
    { "_speed", UB_SPEED },
    { "_zoom", UB_ZOOM },
    { "_showScores", UB_SHOWSCORES },
    { "_use", UB_USE },

    { "_impulse0", UB_IMPULSE0 },
    { "_impulse1", UB_IMPULSE1 },
    { "_impulse2", UB_IMPULSE2 },
    { "_impulse3", UB_IMPULSE3 },
    { "_impulse4", UB_IMPULSE4 },
    { "_impulse5", UB_IMPULSE5 },
    { "_impulse6", UB_IMPULSE6 },
    { "_impulse7", UB_IMPULSE7 },
    { "_impulse8", UB_IMPULSE8 },
    { "_impulse9", UB_IMPULSE9 },
    { "_impulse10", UB_IMPULSE10 },
    { "_impulse11", UB_IMPULSE11 },
    { "_impulse12", UB_IMPULSE12 },
    { "_impulse13", UB_IMPULSE13 },
    { "_impulse14", UB_IMPULSE14 },
    { "_impulse15", UB_IMPULSE15 },
    { "_impulse16", UB_IMPULSE16 },
    { "_impulse17", UB_IMPULSE17 },
    { "_impulse18", UB_IMPULSE18 },
    { "_impulse19", UB_IMPULSE19 },
    { "_impulse20", UB_IMPULSE20 },
    { "_impulse21", UB_IMPULSE21 },
    { "_impulse22", UB_IMPULSE22 },
    { "_impulse23", UB_IMPULSE23 },
    { "_impulse24", UB_IMPULSE24 },
    { "_impulse25", UB_IMPULSE25 },
    { "_impulse26", UB_IMPULSE26 },
    { "_impulse27", UB_IMPULSE27 },
    { "_impulse28", UB_IMPULSE28 },
    { "_impulse29", UB_IMPULSE29 },
    { "_impulse30", UB_IMPULSE30 },
    { "_impulse31", UB_IMPULSE31 },

    { nullptr, UB_NONE },
};
static_assert(ps2::ArrayLength(userCmdStrings) == UB_MAX_BUTTONS);

idCVar in_useJoystick("in_useJoystick", "0", CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM,
                      "controller input is unavailable; changing this preference cannot enable sampling");
idCVar in_joystickRumble("in_joystickRumble", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_ROM,
                         "controller rumble is unavailable; changing this preference cannot enable it");

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("input capability unavailable: %s", method);
}

class UsercmdGen final : public idUsercmdGen
{
  public:
    // No keys, angles, commands, polling buffers or device handles are acquired by this provider.
    void Clear() override {}
    void ClearAngles() override {}
    void Shutdown() override {}

    int CommandStringUsercmdData(const char * command) override
    {
        if (command == nullptr)
        {
            ps2::FatalError("input command string must not be null");
        }
        for (const userCmdString_t & entry : userCmdStrings)
        {
            if (entry.string == nullptr)
            {
                break;
            }
            if (idStr::Icmp(command, entry.string) == 0)
            {
                return entry.button;
            }
        }
        return UB_NONE;
    }

    int ButtonState(int button) override
    {
        // Preserve the native invalid-index result before entering the unavailable sampling path.
        if (button < 0 || button >= UB_MAX_BUTTONS)
        {
            return -1;
        }
        Unsupported("idUsercmdGen::ButtonState");
    }
    int KeyState(int key) override
    {
        if (key < 0 || key >= K_LAST_KEY)
        {
            return -1;
        }
        Unsupported("idUsercmdGen::KeyState");
    }

    void Init() override { Unsupported("idUsercmdGen::Init"); }
    void InitForNewMap() override { Unsupported("idUsercmdGen::InitForNewMap"); }
    void InhibitUsercmd(inhibit_t, bool) override { Unsupported("idUsercmdGen::InhibitUsercmd"); }
    void MouseState(int *, int *, int *, bool *) override { Unsupported("idUsercmdGen::MouseState"); }
    void BuildCurrentUsercmd(int) override { Unsupported("idUsercmdGen::BuildCurrentUsercmd"); }
    usercmd_t GetCurrentUsercmd() override { Unsupported("idUsercmdGen::GetCurrentUsercmd"); }
};

static UsercmdGen s_usercmdGen;
} // namespace

idUsercmdGen * usercmdGen = &s_usercmdGen;
