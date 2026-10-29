// ================================================================================================
// File: keyboard.cpp
// Brief: ps2kbd bring-up, idle/active polling and the USB HID usage to engine key translation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/keyboard.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <libkbd.h>
#include <loadfile.h>

namespace ps2::input::keyboard
{
namespace
{

// A frame reads at most this many raw keys and posts at most this many events: the driver sends
// two per keystroke and does not repeat in raw mode, so only a long stall fills them.
static constexpr int kMaxReadsPerFrame = 64;
static constexpr int kMaxEventsPerFrame = 32;

// Every read is an IOP file round trip, and the driver cannot say whether a keyboard is attached.
// An idle keyboard is read every kIdlePollMsec; one in use every frame, until kActiveMsec after
// the last raw key and for as long as any key is held.
static constexpr int kIdlePollMsec = 250;
static constexpr int kActiveMsec = 10 * 1000;
static constexpr u32 kUsageCount = 256;

static bool s_available = false;
static bool s_usbdStarted = false;
static bool s_kbdStarted = false;
static bool s_held[kUsageCount] = {};
static bool s_keySeen = false;
static bool s_shift = false;
static int s_lastKeyMsec = 0;
static int s_lastPollMsec = 0;
static ps2::input::keyboard::Stats s_stats = {};

struct UsageKey
{
    u8 usage;
    u8 key;
    char plain;
    char shifted;
};

// The irregular part of HID usage page 7, with each printable key's US characters. Letters,
// digits and F1..F10 are contiguous and filled in below. Lock keys, the GUI keys and the
// international keys have no engine key and are dropped.
static constexpr UsageKey kUsageKeys[] = {
    { 0x27, K_0, '0', ')' },
    { 0x28, K_ENTER, 0, 0 },
    { 0x29, K_ESCAPE, 0, 0 },
    { 0x2A, K_BACKSPACE, 8, 8 },
    { 0x2B, K_TAB, 0, 0 },
    { 0x2C, K_SPACE, ' ', ' ' },
    { 0x2D, K_MINUS, '-', '_' },
    { 0x2E, K_EQUALS, '=', '+' },
    { 0x2F, K_LBRACKET, '[', '{' },
    { 0x30, K_RBRACKET, ']', '}' },
    { 0x31, K_BACKSLASH, '\\', '|' },
    { 0x32, K_BACKSLASH, '\\', '|' },
    { 0x33, K_SEMICOLON, ';', ':' },
    // HID 0x34 is the apostrophe. PCSX2's emulated keyboard sends it for the host's grave key,
    // the console toggle, and never sends 0x35; a real keyboard's apostrophe is lost to it.
    { 0x34, K_GRAVE, '`', '~' },
    { 0x35, K_GRAVE, '`', '~' },
    { 0x36, K_COMMA, ',', '<' },
    { 0x37, K_PERIOD, '.', '>' },
    { 0x38, K_SLASH, '/', '?' },
    { 0x39, K_CAPSLOCK, 0, 0 },
    { 0x44, K_F11, 0, 0 },
    { 0x45, K_F12, 0, 0 },
    { 0x46, K_PRINTSCREEN, 0, 0 },
    { 0x47, K_SCROLL, 0, 0 },
    { 0x48, K_PAUSE, 0, 0 },
    { 0x49, K_INS, 0, 0 },
    { 0x4A, K_HOME, 0, 0 },
    { 0x4B, K_PGUP, 0, 0 },
    { 0x4C, K_DEL, 0, 0 },
    { 0x4D, K_END, 0, 0 },
    { 0x4E, K_PGDN, 0, 0 },
    { 0x4F, K_RIGHTARROW, 0, 0 },
    { 0x50, K_LEFTARROW, 0, 0 },
    { 0x51, K_DOWNARROW, 0, 0 },
    { 0x52, K_UPARROW, 0, 0 },
    { 0x53, K_NUMLOCK, 0, 0 },
    { 0x54, K_KP_SLASH, '/', '/' },
    { 0x55, K_KP_STAR, '*', '*' },
    { 0x56, K_KP_MINUS, '-', '-' },
    { 0x57, K_KP_PLUS, '+', '+' },
    { 0x58, K_KP_ENTER, 0, 0 },
    { 0x59, K_KP_1, '1', '1' },
    { 0x5A, K_KP_2, '2', '2' },
    { 0x5B, K_KP_3, '3', '3' },
    { 0x5C, K_KP_4, '4', '4' },
    { 0x5D, K_KP_5, '5', '5' },
    { 0x5E, K_KP_6, '6', '6' },
    { 0x5F, K_KP_7, '7', '7' },
    { 0x60, K_KP_8, '8', '8' },
    { 0x61, K_KP_9, '9', '9' },
    { 0x62, K_KP_0, '0', '0' },
    { 0x63, K_KP_DOT, '.', '.' },
    { 0x64, K_BACKSLASH, '\\', '|' },
    { 0xE0, K_LCTRL, 0, 0 },
    { 0xE1, K_LSHIFT, 0, 0 },
    { 0xE2, K_LALT, 0, 0 },
    { 0xE4, K_RCTRL, 0, 0 },
    { 0xE5, K_RSHIFT, 0, 0 },
    { 0xE6, K_RALT, 0, 0 },
};

// Engine keys are scan codes in QWERTY order; HID letters are alphabetical.
static constexpr u8 kLetterKeys[26] = {
    K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
    K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z
};

static constexpr char kDigitShifted[9] = { '!', '@', '#', '$', '%', '^', '&', '*', '(' };

struct KeyTable
{
    u8 key[kUsageCount] = {};
    char plain[kUsageCount] = {};
    char shifted[kUsageCount] = {};
};

static KeyTable s_table;

PS2_COLD_FUNC void BuildKeyTable()
{
    s_table = KeyTable();

    for (u32 index = 0; index < 26; ++index)
    {
        s_table.key[0x04 + index] = kLetterKeys[index];
        s_table.plain[0x04 + index] = static_cast<char>('a' + index);
        s_table.shifted[0x04 + index] = static_cast<char>('A' + index);
    }

    for (u32 index = 0; index < 9; ++index)
    {
        s_table.key[0x1E + index] = static_cast<u8>(K_1 + index);
        s_table.plain[0x1E + index] = static_cast<char>('1' + index);
        s_table.shifted[0x1E + index] = kDigitShifted[index];
    }

    for (u32 index = 0; index < 10; ++index)
    {
        s_table.key[0x3A + index] = static_cast<u8>(K_F1 + index);
    }

    for (const UsageKey & entry : kUsageKeys)
    {
        s_table.key[entry.usage] = entry.key;
        s_table.plain[entry.usage] = entry.plain;
        s_table.shifted[entry.usage] = entry.shifted;
    }
}

PS2_COLD_FUNC bool StartModule(const char * irxDirectory, const char * name, bool & started)
{
    if (started)
    {
        return true;
    }

    char path[128];
    idStr::snPrintf(path, static_cast<int>(sizeof(path)), "%s/%s.irx", irxDirectory, name);

    int moduleResult = 0;
    const int id = SifLoadStartModule(path, 0, nullptr, &moduleResult);

    // A negative id is a load that failed; result 1 is a module whose start bailed out.
    if (id < 0 || moduleResult == 1)
    {
        ps2::Log(ps2::LogLevel::Warning, "[D3BFG] INPUT keyboard module %s failed: id=%d result=%d\n", path, id, moduleResult);
        return false;
    }

    started = true;
    return true;
}

} // namespace

PS2_COLD_FUNC bool Init(const char * irxDirectory)
{
    if (s_available || irxDirectory == nullptr) [[unlikely]]
    {
        FatalError("keyboard initialized twice or without an IRX directory");
    }

    BuildKeyTable();
    s_stats = Stats();
    s_keySeen = false;
    s_shift = false;
    s_lastPollMsec = 0;
    memset(static_cast<void *>(s_held), 0, sizeof(s_held));

    // ps2kbd speaks USB, so usbd goes first. A driver that will not start only costs the keyboard.
    if (!StartModule(irxDirectory, "usbd", s_usbdStarted) || !StartModule(irxDirectory, "ps2kbd", s_kbdStarted))
    {
        return false;
    }

    if (PS2KbdInit() == 0)
    {
        Log(LogLevel::Warning, "[D3BFG] INPUT keyboard driver did not open\n");
        return false;
    }

    // Raw mode: usages with an up/down state instead of cooked ASCII. Non-blocking reads, so an
    // empty queue returns at once; the flush drops anything queued before the switch.
    PS2KbdSetReadmode(PS2KBD_READMODE_RAW);
    PS2KbdSetBlockingMode(PS2KBD_NONBLOCKING);
    PS2KbdFlushBuffer();
    s_available = true;
    s_stats.available = true;
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (s_available)
    {
        PS2KbdClose();
        s_available = false;
    }

    s_stats.available = false;
    s_stats.held = 0;
    s_keySeen = false;
    s_shift = false;

    memset(static_cast<void *>(s_held), 0, sizeof(s_held));
}

void Poll(int nowMsec)
{
    if (!s_available)
    {
        return;
    }

    const bool inUse = s_stats.held > 0 || (s_keySeen && nowMsec - s_lastKeyMsec < kActiveMsec);

    if (!inUse && nowMsec - s_lastPollMsec < kIdlePollMsec)
    {
        return;
    }

    s_lastPollMsec = nowMsec;
    ++s_stats.polls;
    int events = 0;

    for (int reads = 0; reads < kMaxReadsPerFrame && events < kMaxEventsPerFrame; ++reads)
    {
        PS2KbdRawKey raw = {};

        if (PS2KbdReadRaw(&raw) <= 0)
        {
            break;
        }

        const bool down = raw.state == PS2KBD_RAWKEY_DOWN;
        const u32 usage = raw.key;
        ++s_stats.rawKeys;
        s_keySeen = true;
        s_lastKeyMsec = nowMsec;

        if (s_held[usage] != down)
        {
            s_held[usage] = down;
            s_stats.held += down ? 1U : 0U;
            s_stats.held -= down ? 0U : 1U;
        }

        if (usage == 0xE1 || usage == 0xE5)
        {
            s_shift = s_held[0xE1] || s_held[0xE5];
        }

        const int key = s_table.key[usage];

        if (key == 0)
        {
            continue;
        }

        Sys_QueEvent(SE_KEY, key, down ? 1 : 0, 0, nullptr, 0);
        ++events;
        const char character = s_shift ? s_table.shifted[usage] : s_table.plain[usage];

        // The console's edit field takes typed characters and backspace as character events.
        if (down && character != 0)
        {
            Sys_QueEvent(SE_CHAR, character, 0, 0, nullptr, 0);
            ++events;
        }
    }

    s_stats.events += static_cast<u32>(events);
}

const Stats & GetStats()
{
    return s_stats;
}

} // namespace ps2::input::keyboard
