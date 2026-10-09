// ================================================================================================
// File: sys_services.cpp
// Brief: Supply portable language/time metadata and explicit failures for unavailable OS services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

namespace
{
static constexpr const char * kLanguages[] = {
    ID_LANG_ENGLISH, ID_LANG_FRENCH, ID_LANG_ITALIAN, ID_LANG_GERMAN, ID_LANG_SPANISH, ID_LANG_JAPANESE, nullptr
};
static constexpr int kLanguageCount = static_cast<int>(ps2::ArrayLength(kLanguages)) - 1;
static char s_seconds[64];
static char s_timestamp[64];

[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("platform capability unavailable: %s", method);
}
} // namespace

idCVar sys_lang("sys_lang", ID_LANG_ENGLISH, CVAR_SYSTEM | CVAR_INIT, "language identifier for resource lookup");

int Sys_NumLangs() { return kLanguageCount; }
const char * Sys_Lang(int index) { return index >= 0 && index < kLanguageCount ? kLanguages[index] : ""; }
const char * Sys_DefaultLanguage() { return ID_LANG_ENGLISH; }

const char * Sys_SecToStr(int seconds)
{
    if (seconds < 0)
    {
        ps2::FatalError("Sys_SecToStr requires a nonnegative duration");
    }
    const int weeks = seconds / 604800;
    const int days = seconds / 86400 % 7;
    const int hours = seconds / 3600 % 24;
    const int minutes = seconds / 60 % 60;
    const int remaining = seconds % 60;
    if (weeks != 0)
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%dw, %dd, %d:%02d:%02d", weeks, days, hours, minutes, remaining);
    }
    else if (days != 0)
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%dd, %d:%02d:%02d", days, hours, minutes, remaining);
    }
    else
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%d:%02d:%02d", hours, minutes, remaining);
    }
    return s_seconds;
}

const char * Sys_TimeStampToStr(ID_TIME_T timestamp)
{
    // Format supplied values in UTC; this does not validate storage-driver timestamps or a wall clock.
    const time_t value = static_cast<time_t>(timestamp);
    if (timestamp < 0 || static_cast<ID_TIME_T>(value) != timestamp)
    {
        return "timestamp unavailable";
    }
    const tm * calendar = gmtime(&value);
    if (calendar == nullptr || strftime(s_timestamp, sizeof(s_timestamp), "%Y-%m-%d %H:%M UTC", calendar) == 0)
    {
        return "timestamp unavailable";
    }
    return s_timestamp;
}

void Sys_Quit() { Unsupported("Sys_Quit"); }
void Sys_Launch(const char *, idCmdArgs &, void *, unsigned int) { Unsupported("Sys_Launch"); }
char * Sys_GetClipboardData() { Unsupported("Sys_GetClipboardData"); }
void Sys_GrabMouseCursor(bool) { Unsupported("Sys_GrabMouseCursor"); }
void Sys_SetRumble(int, int, int) { Unsupported("Sys_SetRumble"); }
void Sys_SetPhysicalWorkMemory(int, int) { Unsupported("Sys_SetPhysicalWorkMemory"); }
int Sys_GetDriveFreeSpace(const char *) { Unsupported("Sys_GetDriveFreeSpace"); }
int64 Sys_GetDriveFreeSpaceInBytes(const char *) { Unsupported("Sys_GetDriveFreeSpaceInBytes"); }
