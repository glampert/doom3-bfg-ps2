// ================================================================================================
// File: voice_timeline_tests.cpp
// Brief: Run shared playback cases and require invalid timeline inputs to terminate in either assertion mode.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/audio/voice_timeline.h"
#include "tests/smoketests/voice_tests.h"
#include <bit>
#include <cstring>

int main(int argc, char ** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    if (std::strcmp(argv[1], "valid") == 0)
    {
        return ps2::smoketests::RunVoiceTimelineTests() ? 0 : 1;
    }
    ps2::audio::VoiceTimeline voice;
    if (std::strcmp(argv[1], "unconfigured") == 0)
    {
        voice.Start(0, 0);
    }
    if (std::strcmp(argv[1], "sample") == 0)
    {
        voice.Configure({});
    }
    voice.Configure({ 8000, 8000 });
    voice.Start(0, 100);
    if (std::strcmp(argv[1], "clock") == 0)
    {
        voice.Advance(99);
    }
    if (std::strcmp(argv[1], "offset") == 0)
    {
        voice.Start(-1, 100);
    }
    if (std::strcmp(argv[1], "negative-pitch") == 0)
    {
        voice.SetPitch(-0.5f, 100);
    }
    if (std::strcmp(argv[1], "large-pitch") == 0)
    {
        voice.SetPitch(9.0f, 100);
    }
    if (std::strcmp(argv[1], "nan-pitch") == 0)
    {
        voice.SetPitch(std::bit_cast<float>(0x7fc00000u), 100);
    }
    if (std::strcmp(argv[1], "inf-pitch") == 0)
    {
        voice.SetPitch(std::bit_cast<float>(0x7f800000u), 100);
    }
    return 1; // An expected fatal input returning normally must fail the subprocess test.
}
