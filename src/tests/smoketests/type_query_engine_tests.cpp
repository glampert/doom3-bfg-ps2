// ================================================================================================
// File: type_query_engine_tests.cpp
// Brief: Check real menu/GUI/model hierarchy declarations and real polymorphic file objects on EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include <idlib/precompiled.h>
#include <d3xp/Game_local.h>
#include <ui/Winvar.h>
#include <renderer/Model_local.h>
#include "tests/smoketests/type_query_tests.h"
#include "ps2/system/log.h"

namespace ps2::smoketests
{
namespace
{
bool Check(bool condition, const char * name)
{
    Log(LogLevel::Info, "[D3BFG] CHECK types/%s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}
}

bool RunEngineTypeQueryTests()
{
    bool passed = Check(idMenuWidget_ScoreboardList::MatchesType(TypeKey<idMenuWidget_DynamicList>()) &&
        idMenuWidget_ScoreboardList::MatchesType(TypeKey<idMenuWidget_List>()) &&
        idMenuWidget_LobbyButton::MatchesType(TypeKey<idMenuWidget_Button>()) &&
        !idMenuWidget_NavButton::MatchesType(TypeKey<idMenuWidget_MenuButton>()) &&
        idMenuScreen_PDA_VideoDisks::MatchesType(TypeKey<idMenuWidget>()) &&
        !idMenuScreen_PDA_VideoDisks::MatchesType(TypeKey<idMenuScreen_PDA_UserData>()) &&
        idMenuHandler_PDA::MatchesType(TypeKey<idMenuHandler>()) &&
        !idMenuHandler_PDA::MatchesType(TypeKey<idMenuHandler_HUD>()), "menu-hierarchy");
    passed &= Check(idWinBackground::MatchesType(TypeKey<idWinStr>()) &&
        idWinBackground::MatchesType(TypeKey<idWinVar>()) &&
        !idWinStr::MatchesType(TypeKey<idWinBackground>()) &&
        !idWinRectangle::MatchesType(TypeKey<idWinVec4>()), "gui-hierarchy");
    passed &= Check(idRenderModelMD5::MatchesType(TypeKey<idRenderModelStatic>()) &&
        idRenderModelMD5::MatchesType(TypeKey<idRenderModel>()) &&
        !idRenderModelStatic::MatchesType(TypeKey<idRenderModelMD5>()) &&
        !idRenderModelBeam::MatchesType(TypeKey<idRenderModelSprite>()), "model-hierarchy");
    {
        idFile file;
        idFile_Memory memory;
        idFile * base = &memory;
        passed &= Check(CheckedCast<idFile_Memory *>(base) == &memory &&
            CheckedCast<const idFile_Memory *>(static_cast<const idFile *>(base)) == &memory &&
            CheckedCast<idFile_Memory *>(&file) == nullptr &&
            CheckedCast<idFile_Permanent *>(base) == nullptr &&
            memory.IsType(FileTypeKeyFromOtherUnit()), "real-file-objects");
    }
    return passed;
}

} // namespace ps2::smoketests
