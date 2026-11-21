// ================================================================================================
// File: log_mirror.cpp
// Brief: The console run's log on the boot screen and in the package's d3bfg.log (log_mirror.h).
//        Assertion builds only.
//
//  The file follows quake2-ps2's console log: every write opens the file, appends and closes it,
//  because closing is the only way to get a line onto a USB drive. FatFs keeps a file's last
//  partial sector in its own buffer and writes the file's size into its directory entry only on a
//  sync or a close; bdmfs_fatfs has no sync (EIO) and libcglue's fsync() is ENOSYS. BDM's block
//  cache writes through, so a closed line is on the drive. PFS keeps directory metadata cached past
//  the close under the boot's read/write mount, so a package on pfs0: syncs after each close. That
//  costs a few USB commands a line, about 23 ms on a console (quake2-ps2, 2026-10-07): a debug
//  boot logs some 250 lines before its first frame.
//
//  Neither reader may allocate or log: LogV is the sink everything else reports through.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/debug/log_mirror.h"

#if PS2_D3BFG_ASSERTS

#include "ps2/debug/scr_print.h"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <kernel.h>
#include <timer.h>
#include <unistd.h>

// The header refuses direct fileXio use beside the C library unless told the caller knows: the
// file goes through the C library, and only the PFS sync is a direct call.
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

namespace ps2::debug
{
namespace
{

static constexpr const char * kLogFileName = "d3bfg.log";

// The screen's text colours, 0xAABBGGRR.
static constexpr u32 kWhite = 0xFFFFFFFF;
static constexpr u32 kYellow = 0xFF00FFFF;
static constexpr u32 kRed = 0xFF0000FF;

// Holds what the boot logs before the drive is found (about 30 KB for a retail world package),
// and later each message on its way to the file.
static constexpr int kBufferBytes = 48 * 1024;

enum class FileState
{
    Buffering, // no package yet: lines wait in the buffer
    Pending,   // the first write creates the file, emptying an old one
    Open,      // every write since has gone in
    Off,       // host:, or a write failed
};

static bool s_screen = false;
static FileState s_state = FileState::Buffering;
static char s_path[256] = {};
static char s_syncDevice[16] = {};
static char s_buffer[kBufferBytes];
static int s_used = 0;
static int s_dropped = 0;
static bool s_atLineStart = true;

PS2_COLD_FUNC int NowMsec()
{
    u32 seconds = 0;
    u32 microseconds = 0;
    TimerBusClock2USec(GetTimerSystemTime(), &seconds, &microseconds);
    return static_cast<int>(seconds * 1000u + microseconds / 1000u);
}

// Appends to the file, which the first write creates. Failures go to stdout, not the log.
void WriteOut(const char * data, int bytes)
{
    const bool create = s_state == FileState::Pending;
    const int fd = open(s_path, create ? (O_WRONLY | O_CREAT | O_TRUNC) : O_WRONLY, 0666);
    bool written = false;
    int syncResult = 0;

    if (fd >= 0)
    {
        written = lseek(fd, 0, SEEK_END) >= 0 && write(fd, data, static_cast<size_t>(bytes)) == bytes;

        // Only a negative close fails: through fileXio, iomanX returns the descriptor's slot.
        const int closeResult = close(fd);

        // Even a partial write may have changed PFS metadata.
        syncResult = s_syncDevice[0] != '\0' ? fileXioSync(s_syncDevice, FXIO_WAIT) : 0;
        written = written && closeResult >= 0 && syncResult == 0;
    }

    if (written)
    {
        s_state = FileState::Open;
        return;
    }

    // A drive that refused one write would hold up every later line.
    std::printf("[D3BFG] WARNING can't write the log file %s (open %d, sync %d): logging to it is off\n", s_path, fd, syncResult);
    s_state = FileState::Off;
}

void Flush()
{
    if (s_used > 0 && (s_state == FileState::Pending || s_state == FileState::Open))
    {
        WriteOut(s_buffer, s_used);
    }

    s_used = 0;
}

bool Put(char letter)
{
    if (s_used == kBufferBytes)
    {
        if (s_state == FileState::Buffering)
        {
            ++s_dropped;
            return false;
        }

        Flush();

        if (s_state == FileState::Off)
        {
            return false;
        }
    }

    s_buffer[s_used++] = letter;
    return true;
}

void PutText(const char * text)
{
    for (; *text != '\0'; ++text)
    {
        if (!Put(*text))
        {
            return;
        }
    }
}

// Each line gets the seconds since the EE timer started.
void PutLine(const char * text, int msec)
{
    for (; *text != '\0'; ++text)
    {
        if (s_atLineStart)
        {
            char stamp[24];
            std::snprintf(stamp, sizeof(stamp), "[%4d.%03d] ", msec / 1000, msec % 1000);
            PutText(stamp);
            s_atLineStart = false;
        }

        if (!Put(*text))
        {
            return;
        }

        s_atLineStart = *text == '\n';
    }
}

} // namespace

PS2_COLD_FUNC void BeginBootScreen()
{
    ScrInit();
    s_screen = true;
}

PS2_COLD_FUNC void EndBootScreen()
{
    s_screen = false;
}

PS2_COLD_FUNC void OpenLogFile(const char * packageRoot)
{
    if (s_state != FileState::Buffering) [[unlikely]]
    {
        return;
    }

    if (std::strncmp(packageRoot, "host:", 5) == 0)
    {
        s_state = FileState::Off;
        s_used = 0;
        return;
    }

    std::snprintf(s_path, sizeof(s_path), "%s/%s", packageRoot, kLogFileName);

    // A package on the hard disk is under the mount the boot keeps ("pfs0:").
    const char * colon = std::strchr(packageRoot, ':');

    if (std::strncmp(packageRoot, "pfs", 3) == 0 && colon != nullptr && colon - packageRoot + 1 < static_cast<int>(sizeof(s_syncDevice)))
    {
        std::memcpy(s_syncDevice, packageRoot, static_cast<size_t>(colon - packageRoot + 1));
        s_syncDevice[colon - packageRoot + 1] = '\0';
    }

    // The header creates the file, then the lines the boot logged until now follow it.
    char header[sizeof(s_path) + 128];
    const int bytes = std::snprintf(header, sizeof(header), "Doom 3 BFG for PS2 (debug build), package on %s; %d bytes of the boot's log dropped\n",
                                    packageRoot, s_dropped);

    s_state = FileState::Pending;
    WriteOut(header, bytes < static_cast<int>(sizeof(header)) ? bytes : static_cast<int>(sizeof(header)) - 1);
    Flush();
}

void MirrorLog(LogLevel level, const char * text)
{
    if (s_screen)
    {
        const u32 color = level == LogLevel::Info ? kWhite : (level == LogLevel::Warning ? kYellow : kRed);
        ScrSetTextColor(color);
        ScrPrintf("%s", text);
    }

    if (s_state == FileState::Off)
    {
        return;
    }

    PutLine(text, NowMsec());

    if (s_state != FileState::Buffering)
    {
        Flush();
    }
}

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
