// ================================================================================================
// File: voice_tests.cpp
// Brief: Check frame completion, pitch history, pause, looping offsets and extreme clocks with real timeline code.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/voice_tests.h"
#include "ps2/audio/voice_timeline.h"
#include "ps2/system/log.h"
#include <climits>
#include <cstdint>

namespace
{
using ps2::audio::VoiceSegment;
using ps2::audio::VoiceState;
using ps2::audio::VoiceTimeline;

bool At(const VoiceTimeline & voice, VoiceState state, VoiceSegment segment, std::uint32_t frame)
{
    const auto cursor = voice.GetCursor();
    return cursor.state == state && cursor.segment == segment && cursor.frame == frame;
}

bool OneShot()
{
    VoiceTimeline voice;
    voice.Configure({ 9, 8000 }); // 1.125 ms; integer duration must not truncate playback to 1 ms.
    voice.Start(0, 0);
    voice.Advance(1124);
    bool passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 8);
    voice.Advance(1125);
    passed = At(voice, VoiceState::Complete, VoiceSegment::None, 0) && passed;
    voice.Pause(1200);
    voice.UnPause(1300);
    passed = At(voice, VoiceState::Complete, VoiceSegment::None, 0) && passed;
    voice.Start(1, 2000);
    voice.Advance(2124);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 8) && passed;
    voice.Advance(2125);
    passed = At(voice, VoiceState::Complete, VoiceSegment::None, 0) && passed;
    voice.Start(2, 3000);
    passed = At(voice, VoiceState::Complete, VoiceSegment::None, 0) && passed;
    voice.Stop(3000);
    return At(voice, VoiceState::Idle, VoiceSegment::None, 0) && passed;
}

bool Loops()
{
    VoiceTimeline voice;
    voice.Configure({ 11025, 11025 }, { 800, 8000 }); // 1 s leadin, then 100 ms loops at a different rate.
    voice.Start(0, 0);
    voice.Advance(999999);
    bool passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 11024);
    voice.Advance(1000000);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Loop, 0) && passed;
    voice.Advance(1150000);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Loop, 400) && passed;
    voice.Start(1150, 1200000); // Seek subtracts the leadin before wrapping the loop.
    passed = At(voice, VoiceState::Playing, VoiceSegment::Loop, 400) && voice.GetCursor().timeMS == 50 && passed;
    voice.Advance(1250000);
    return At(voice, VoiceState::Playing, VoiceSegment::Loop, 0) && passed;
}

bool PauseAndStop()
{
    VoiceTimeline voice;
    voice.Configure({ 1000, 8000 });
    voice.Start(0, 0);
    voice.Pause(20000);
    voice.Advance(500000);
    voice.Pause(500000);
    bool passed = At(voice, VoiceState::Paused, VoiceSegment::Leadin, 160);
    voice.UnPause(550000);
    voice.UnPause(550000);
    voice.Advance(555000);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 200) && passed;
    voice.Stop(555000);
    voice.UnPause(600000);
    return At(voice, VoiceState::Idle, VoiceSegment::None, 0) && passed;
}

bool PitchHistory()
{
    VoiceTimeline voice;
    voice.Configure({ 8000, 8000 });
    voice.SetPitch(0.5f, 0);
    voice.Start(0, 0);
    for (std::uint64_t time = 1; time <= 1000; ++time)
    {
        voice.Advance(time);
    }
    bool passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 4) && voice.GetPitch() == 0.5f;
    voice.SetPitch(2.0f, 2000); // The previous millisecond still belongs to the old pitch.
    passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 8) && passed;
    voice.Advance(3000);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 24) && passed;
    voice.SetPitch(0.0f, 3000);
    voice.Advance(1000000);
    passed = At(voice, VoiceState::Playing, VoiceSegment::Leadin, 24) && passed;
    voice.SetPitch(1.0f, 1000000);
    voice.Advance(1001000);
    return At(voice, VoiceState::Playing, VoiceSegment::Leadin, 32) && passed;
}

bool ExtremeClocks()
{
    VoiceTimeline voice;
    voice.Configure({ 8000, 8000 }, { 4000, 8000 });
    voice.SetPitch(8.0f, 0);
    voice.Start(0, 0);
    voice.Advance(UINT64_MAX);
    const std::uint64_t microseconds = (UINT64_MAX % 500000) * 8 % 500000;
    const auto expected = static_cast<std::uint32_t>(microseconds * 8000 / 1000000);
    bool passed = At(voice, VoiceState::Playing, VoiceSegment::Loop, expected);
    voice.Start(INT_MAX, UINT64_MAX);
    const std::uint64_t seek = (static_cast<std::uint64_t>(INT_MAX) * 1000 - 1000000) % 500000;
    passed = At(voice, VoiceState::Playing, VoiceSegment::Loop,
                static_cast<std::uint32_t>(seek * 8000 / 1000000)) &&
             passed;
    voice.Configure({ 8, 8000 });
    voice.Start(0, UINT64_MAX - 1000);
    voice.Advance(UINT64_MAX);
    return At(voice, VoiceState::Complete, VoiceSegment::None, 0) && passed;
}

bool UpdateCadence()
{
    VoiceTimeline stepped, single;
    stepped.Configure({ 11026, 11025 }, { 4411, 44100 });
    single.Configure({ 11026, 11025 }, { 4411, 44100 });
    stepped.SetPitch(1.125f, 0);
    single.SetPitch(1.125f, 0);
    stepped.Start(0, 0);
    single.Start(0, 0);
    for (std::uint64_t time = 997; time < 2000000; time += 997)
    {
        stepped.Advance(time);
    }
    stepped.Advance(2000000);
    single.Advance(2000000);
    const auto a = stepped.GetCursor(), b = single.GetCursor();
    return a.state == b.state && a.segment == VoiceSegment::Loop && a.segment == b.segment &&
           a.frame == b.frame && a.timeMS == b.timeMS;
}
} // namespace

namespace ps2::smoketests
{
bool RunVoiceTimelineTests()
{
    bool passed = true;
    const bool results[] = { OneShot(), Loops(), PauseAndStop(), PitchHistory(), ExtremeClocks(), UpdateCadence() };
    for (bool result : results)
    {
        passed = result && passed;
    }
    Log(LogLevel::Info, "[D3BFG] CHECK audio/voice-timeline %s\n", passed ? "PASS" : "FAIL");
    return passed;
}
} // namespace ps2::smoketests
