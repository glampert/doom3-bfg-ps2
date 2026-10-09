// ================================================================================================
// File: common_foundation.cpp
// Brief: Bind the real Common class to foundation diagnostics and explicit deferred capabilities.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/lifecycle.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Common_local.h relies on the complete engine precompiled header.
#include <framework/Common_local.h>

namespace
{
static constexpr size_t kPrintCapacity = 2048;
[[noreturn]] void Unsupported(const char * operation)
{
    ps2::FatalError("unsupported Common foundation operation: %s", operation);
}
} // namespace

#define CORE_UNSUPPORTED(result, method, parameters) \
    result idCommonLocal::method parameters { Unsupported(#method); }

void idCommonLocal::Printf(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    VPrintf(format, arguments);
    va_end(arguments);
}

void idCommonLocal::VPrintf(const char * format, va_list arguments)
{
    if (m_redirectBuffer == nullptr)
    {
        ps2::LogV(ps2::LogLevel::Info, format, arguments);
        return;
    }
    char message[kPrintCapacity] = {};
    idStr::vsnPrintf(message, static_cast<int>(sizeof(message)), format, arguments);
    for (const char * cursor = message; *cursor != '\0'; ++cursor)
    {
        if (m_redirectLength == m_redirectCapacity - 1)
        {
            FlushRedirect();
        }
        m_redirectBuffer[m_redirectLength++] = *cursor;
        m_redirectBuffer[m_redirectLength] = '\0';
    }
}

void idCommonLocal::BeginRedirect(char * buffer, int capacity, void (*flush)(const char *))
{
    if (buffer == nullptr || capacity < 2 || flush == nullptr || m_redirectBuffer != nullptr)
    {
        Unsupported("invalid/nested console redirect");
    }
    m_redirectBuffer = buffer;
    m_redirectCapacity = static_cast<size_t>(capacity);
    m_redirectLength = 0;
    m_redirectFlush = flush;
    buffer[0] = '\0';
}

void idCommonLocal::EndRedirect()
{
    if (m_redirectBuffer != nullptr)
    {
        FlushRedirect();
        m_redirectBuffer = nullptr;
        m_redirectFlush = nullptr;
    }
}

void idCommonLocal::DPrintf(const char * format, ...)
{
    if (!cvarSystem->IsInitialized() || !cvarSystem->GetCVarBool("developer"))
    {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    VPrintf(format, arguments);
    va_end(arguments);
}

void idCommonLocal::Warning(const char * format, ...)
{
    ++m_warningCount;
    va_list arguments;
    va_start(arguments, format);
    VWarning(format, arguments);
    va_end(arguments);
}

void idCommonLocal::DWarning(const char * format, ...)
{
    if (!cvarSystem->IsInitialized() || !cvarSystem->GetCVarBool("developer"))
    {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    VWarning(format, arguments);
    va_end(arguments);
}

void idCommonLocal::Error(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    ps2::FatalErrorV(format, arguments);
}

void idCommonLocal::FatalError(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    ps2::FatalErrorV(format, arguments);
}

void idCommonLocal::VWarning(const char * format, va_list arguments)
{
    if (m_redirectBuffer == nullptr)
    {
        ps2::LogV(ps2::LogLevel::Warning, format, arguments);
        return;
    }
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WARNING ");
    VPrintf(format, arguments);
    ps2::Log(ps2::LogLevel::Info, "\n");
}

void idCommonLocal::FlushRedirect()
{
    m_redirectFlush(m_redirectBuffer);
    m_redirectLength = 0;
    m_redirectBuffer[0] = '\0';
}

CORE_UNSUPPORTED(void, CreateMainMenu, ())
CORE_UNSUPPORTED(void, Quit, ())
CORE_UNSUPPORTED(void, UpdateScreen, (bool))
CORE_UNSUPPORTED(void, UpdateLevelLoadPacifier, ())
CORE_UNSUPPORTED(const char *, KeysFromBinding, (const char *))
CORE_UNSUPPORTED(const char *, BindingFromKey, (const char *))
CORE_UNSUPPORTED(int, ButtonState, (int))
CORE_UNSUPPORTED(int, KeyState, (int))
CORE_UNSUPPORTED(bool, ProcessEvent, (const sysEvent_t *))
CORE_UNSUPPORTED(bool, LoadGame, (const char *))
CORE_UNSUPPORTED(bool, SaveGame, (const char *))
CORE_UNSUPPORTED(void, OnSaveCompleted, (idSaveLoadParms &))
CORE_UNSUPPORTED(void, OnLoadCompleted, (idSaveLoadParms &))
CORE_UNSUPPORTED(void, OnLoadFilesCompleted, (idSaveLoadParms &))
CORE_UNSUPPORTED(void, OnEnumerationCompleted, (idSaveLoadParms &))
CORE_UNSUPPORTED(void, OnDeleteCompleted, (idSaveLoadParms &))
CORE_UNSUPPORTED(void, TriggerScreenWipe, (const char *, bool))
CORE_UNSUPPORTED(void, LaunchExternalTitle, (int, int, const lobbyConnectInfo_t * const))

#undef CORE_UNSUPPORTED

void idCommonLocal::Frame() { ps2::lifecycle::Frame(); }
void idCommonLocal::SwitchToGame(currentGame_t title)
{
    if (title != DOOM3_BFG)
    {
        Unsupported("Classic title switching");
    }
}
bool idCommonLocal::JapaneseCensorship() const { return false; }
void idCommonLocal::ResetPlayerInput(int) { Unsupported("controller input"); }
void idCommonLocal::ClearWipe()
{
    wipeMaterial = nullptr;
    wipeStartTime = wipeStopTime = 0;
    wipeHold = false;
}
int idGameThread::Run() { Unsupported("game worker before game fixture startup"); }

void idCommonLocal::PrintWarnings() { ps2::Log(ps2::LogLevel::Info, "[D3BFG] WARNINGS count=%u\n", m_warningCount); }
void idCommonLocal::ClearWarnings(const char *) { m_warningCount = 0; }
void idCommonLocal::SetRefreshOnPrint(bool refresh)
{
    if (refresh)
    {
        Unsupported("refresh console display");
    }
}
void idCommonLocal::StartupVariable(const char *) {}
void idCommonLocal::WriteConfigToFile(const char *) { Unsupported("config file persistence"); }
