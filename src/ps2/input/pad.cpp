// ================================================================================================
// File: pad.cpp
// Brief: libpad connection state machine and the button and stick to engine key translation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/pad.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <libpad.h>
#include <loadfile.h>

namespace ps2::input::pad
{
namespace
{

static constexpr int kPort = 0;
static constexpr int kSlot = 0;

// A stick past half its range from the center presses the engine's stick key, as the PC does.
static constexpr int kStickCenter = 128;
static constexpr int kStickThreshold = 64;

enum class Status : u8
{
    Unavailable,
    Disconnected,
    SettingMode,
    Ready
};

static Status s_status = Status::Unavailable;
static bool s_modulesLoaded = false;
static bool s_padStarted = false;
static ps2::input::pad::Stats s_stats;

// Every key this pad has pressed since the last poll: buttons and the stick directions.
static bool s_pressed[K_JOY_DPAD_RIGHT + 1 - K_JOY1] = {};

// libpad's transfer area: 256 bytes, 64-byte aligned.
alignas(64) static char s_padArea[256];

struct ButtonKey
{
    u16 button;
    int key;
};

// The PS3 layout the menus report: cross accepts (K_JOY1) and circle backs (K_JOY2).
static constexpr ButtonKey kButtonKeys[] = {
    { PAD_CROSS, K_JOY1 },       { PAD_CIRCLE, K_JOY2 },        { PAD_SQUARE, K_JOY3 },
    { PAD_TRIANGLE, K_JOY4 },    { PAD_L1, K_JOY5 },            { PAD_R1, K_JOY6 },
    { PAD_L3, K_JOY7 },          { PAD_R3, K_JOY8 },            { PAD_START, K_JOY9 },
    { PAD_SELECT, K_JOY10 },     { PAD_L2, K_JOY_TRIGGER1 },    { PAD_R2, K_JOY_TRIGGER2 },
    { PAD_UP, K_JOY_DPAD_UP },   { PAD_DOWN, K_JOY_DPAD_DOWN }, { PAD_LEFT, K_JOY_DPAD_LEFT },
    { PAD_RIGHT, K_JOY_DPAD_RIGHT },
};

bool Connected(int state)
{
    return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
}

PS2_COLD_FUNC bool LoadRomModule(const char * path)
{
    int moduleResult = 0;
    const int id = SifLoadStartModule(path, 0, nullptr, &moduleResult);

    if (id < 0 || moduleResult == 1)
    {
        ps2::Log(ps2::LogLevel::Warning, "[D3BFG] INPUT pad module %s failed: id=%d result=%d\n", path, id, moduleResult);
        return false;
    }

    return true;
}

// Posts a key event when the key's state changed, so a held button sends nothing more.
void SetKey(int key, bool down)
{
    bool & pressed = s_pressed[key - K_JOY1];

    if (pressed == down)
    {
        return;
    }

    pressed = down;
    Sys_QueEvent(SE_KEY, key, down ? 1 : 0, 0, nullptr, 0);
    ++s_stats.events;
}

void ReleaseAll()
{
    for (int key = K_JOY1; key <= K_JOY_DPAD_RIGHT; ++key)
    {
        SetKey(key, false);
    }
}

} // namespace

PS2_COLD_FUNC bool Init()
{
    if (s_status != Status::Unavailable)
    {
        FatalError("pad initialized twice");
    }

    s_stats = Stats();
    memset(static_cast<void *>(s_pressed), 0, sizeof(s_pressed));

    // The ROM's PADMAN runs on the ROM's SIO2MAN; both load once per process. A failure only
    // leaves the pad silent.
    if (!s_modulesLoaded)
    {
        if (!LoadRomModule("rom0:SIO2MAN") || !LoadRomModule("rom0:PADMAN"))
        {
            return false;
        }

        s_modulesLoaded = true;
    }

    if (!s_padStarted)
    {
        if (padInit(0) != 1)
        {
            Log(LogLevel::Warning, "[D3BFG] INPUT pad library did not start\n");
            return false;
        }

        s_padStarted = true;
    }

    if (padPortOpen(kPort, kSlot, s_padArea) == 0)
    {
        Log(LogLevel::Warning, "[D3BFG] INPUT pad port did not open\n");
        return false;
    }

    s_status = Status::Disconnected;
    s_stats.available = true;
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (s_status == Status::Unavailable)
    {
        return;
    }

    ReleaseAll();
    padPortClose(kPort, kSlot);

    s_status = Status::Unavailable;
    s_stats.available = false;
    s_stats.ready = false;
    s_stats.analog = false;
    s_stats.buttons = 0;
}

void Poll()
{
    if (s_status == Status::Unavailable)
    {
        return;
    }

    const int state = padGetState(kPort, kSlot);

    switch (s_status)
    {
        case Status::Disconnected:
            if (Connected(state))
            {
                // Pads start digital; ask a DualShock for its sticks, locked against the MODE button.
                if (padInfoMode(kPort, kSlot, PAD_MODETABLE, -1) > 0)
                {
                    padSetMainMode(kPort, kSlot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
                    s_status = Status::SettingMode;
                }
                else
                {
                    s_status = Status::Ready;
                }
            }

            break;

        case Status::SettingMode:
        {
            const unsigned char request = padGetReqState(kPort, kSlot);

            if ((request == PAD_RSTAT_COMPLETE || request == PAD_RSTAT_FAILED) && Connected(state))
            {
                s_status = Status::Ready;
                Log(LogLevel::Info, "[D3BFG] INPUT pad connected analog=%d\n", request == PAD_RSTAT_COMPLETE ? 1 : 0);
            }

            break;
        }

        case Status::Ready:
            if (state == PAD_STATE_DISCONN)
            {
                ReleaseAll();
                s_stats.buttons = 0;
                s_stats.analog = false;
                s_status = Status::Disconnected;
                break;
            }

            {
                padButtonStatus data = {};

                // A failed read keeps the last state: a transient hiccup, not a release.
                if (padRead(kPort, kSlot, &data) == 0)
                {
                    break;
                }

                const u16 buttons = static_cast<u16>(data.btns ^ 0xFFFFU);
                const int type = data.mode >> 4;
                s_stats.buttons = buttons;
                s_stats.analog = type == PAD_TYPE_ANALOG || type == PAD_TYPE_DUALSHOCK;

                for (const ButtonKey & entry : kButtonKeys)
                {
                    SetKey(entry.key, (buttons & entry.button) != 0);
                }

                const int leftX  = s_stats.analog ? data.ljoy_h - kStickCenter : 0;
                const int leftY  = s_stats.analog ? data.ljoy_v - kStickCenter : 0;
                const int rightX = s_stats.analog ? data.rjoy_h - kStickCenter : 0;
                const int rightY = s_stats.analog ? data.rjoy_v - kStickCenter : 0;

                SetKey(K_JOY_STICK1_UP,    leftY  < -kStickThreshold);
                SetKey(K_JOY_STICK1_DOWN,  leftY  >  kStickThreshold);
                SetKey(K_JOY_STICK1_LEFT,  leftX  < -kStickThreshold);
                SetKey(K_JOY_STICK1_RIGHT, leftX  >  kStickThreshold);
                SetKey(K_JOY_STICK2_UP,    rightY < -kStickThreshold);
                SetKey(K_JOY_STICK2_DOWN,  rightY >  kStickThreshold);
                SetKey(K_JOY_STICK2_LEFT,  rightX < -kStickThreshold);
                SetKey(K_JOY_STICK2_RIGHT, rightX >  kStickThreshold);
            }

            break;

        case Status::Unavailable:
            break;
    }

    s_stats.ready = s_status == Status::Ready;
}

const Stats & GetStats()
{
    return s_stats;
}

} // namespace ps2::input::pad
