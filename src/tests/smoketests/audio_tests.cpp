// ================================================================================================
// File: audio_tests.cpp
// Brief: Prevent unloaded sample metadata from masquerading as loaded timing or an audio device.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/audio_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// This includes the real logical sound declarations and the portable backend seam.
#include <sound/snd_local.h>

namespace ps2::smoketests
{
bool RunAudioTests()
{
    const heap::Stats before = heap::GetTotalStats();
    bool passed = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            idSoundSample sample;
            passed = passed && !sample.IsLoaded() && !sample.IsDefault() && sample.GetName()[0] == '\0' &&
                     sample.GetTimestamp() == FILE_NOT_FOUND_TIMESTAMP && !sample.GetNeverPurge() &&
                     !sample.GetLevelLoadReferenced() && sample.GetLastPlayedTime() == 0;
            const char * name = "fixture/sound/metadata-with-long-name-with-heap-backed-native-string";
            sample.SetName(name);
            sample.SetNeverPurge();
            sample.SetLevelLoadReferenced();
            sample.SetLastPlayedTime(1234);
            passed = passed && idStr::Cmp(sample.GetName(), name) == 0 && sample.GetNeverPurge() &&
                     sample.GetLevelLoadReferenced() && sample.GetLastPlayedTime() == 1234;
            sample.ResetLevelLoadReferenced();
            sample.FreeData();
            passed = passed && !sample.GetLevelLoadReferenced() && !sample.IsLoaded() &&
                     idStr::Cmp(sample.GetName(), name) == 0;
        }
        const heap::Stats after = heap::GetTotalStats();
        passed = passed && before.requestedBytes == after.requestedBytes && before.backingBytes == after.backingBytes &&
                 before.allocationCount == after.allocationCount;
    }
    Log(LogLevel::Info, "[D3BFG] CHECK audio/sample-metadata-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

bool RunAudioFailureProbe(const char * name)
{
    if (idStr::Cmp(name, "audio-resource") == 0)
    {
        idSoundSample sample;
        sample.SetName("fixture/sound/unimplemented");
        sample.LoadResource();
    }
    else if (idStr::Cmp(name, "audio-duration") == 0)
    {
        idSoundSample sample;
        (void)sample.LengthInMsec();
    }
    else if (idStr::Cmp(name, "audio-device") == 0)
    {
        idSoundHardware hardware;
        hardware.Init();
    }
    else
    {
        return false;
    }
    return true; // The caller rejects an unavailable capability returning normally.
}
} // namespace ps2::smoketests
