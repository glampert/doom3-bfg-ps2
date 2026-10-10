// ================================================================================================
// File: audio_tests.cpp
// Brief: Check real PCM timing/amplitude and exact repeated sample ownership without an audio device.
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
namespace
{
bool LedgerMatches(const heap::Stats & before)
{
    const heap::Stats after = heap::GetTotalStats();
    return before.requestedBytes == after.requestedBytes && before.backingBytes == after.backingBytes &&
           before.allocationCount == after.allocationCount;
}

bool PcmSamples()
{
    const heap::Stats before = heap::GetTotalStats();
    const heap::Stats audioBefore = heap::GetStats(TAG_AUDIO);
    bool passed = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            idSoundSample sample;
            sample.SetName("audio-fixtures/mono");
            sample.SetNeverPurge();
            sample.SetLevelLoadReferenced();
            sample.SetLastPlayedTime(77);
            sample.LoadResource();
            passed = sample.IsLoaded() && !sample.IsDefault() && sample.GetTimestamp() != FILE_NOT_FOUND_TIMESTAMP &&
                     sample.SampleRate() == 11025 && sample.NumChannels() == 1 && sample.NumSamples() == 11025 &&
                     sample.BufferSize() == 22050 && sample.LengthInMsec() == 1000 && !sample.IsCompressed() &&
                     sample.GetAmplitude(0) == 1.0f && sample.GetAmplitude(17) == 0.25f && sample.GetAmplitude(34) == 0.0f &&
                     sample.GetAmplitude(999) == 0.5f && sample.GetAmplitude(-1) == 0.0f &&
                     sample.GetAmplitude(1000) == 0.0f && sample.GetAmplitude(INT_MAX) == 0.0f && passed;
            const heap::Stats mono = heap::GetStats(TAG_AUDIO);
            passed = mono.requestedBytes == audioBefore.requestedBytes + 22050 &&
                     mono.allocationCount == audioBefore.allocationCount + 1 && passed;
            // SetName must release the old payload before binding new resource identity.
            sample.SetName("audio-fixtures/stereo");
            passed = !sample.IsLoaded() && sample.GetTimestamp() == FILE_NOT_FOUND_TIMESTAMP && passed;
            sample.LoadResource();
            passed = sample.IsLoaded() && !sample.IsDefault() && sample.SampleRate() == 44100 &&
                     sample.NumChannels() == 2 && sample.NumSamples() == 22050 && sample.BufferSize() == 88200 &&
                     sample.LengthInMsec() == 500 && sample.GetAmplitude(0) == 0.5f && sample.GetAmplitude(499) == 0.5f &&
                     sample.GetAmplitude(500) == 0.0f && sample.GetNeverPurge() && sample.GetLevelLoadReferenced() &&
                     sample.GetLastPlayedTime() == 77 && passed;
            const heap::Stats stereo = heap::GetStats(TAG_AUDIO);
            passed = stereo.requestedBytes == audioBefore.requestedBytes + 88200 &&
                     stereo.allocationCount == audioBefore.allocationCount + 1 && passed;
            sample.LoadResource(); // Reload releases its previous allocation rather than accumulating payloads.
            passed = sample.IsLoaded() && heap::GetStats(TAG_AUDIO).requestedBytes == stereo.requestedBytes && passed;
            sample.FreeData();
            sample.FreeData();
            passed = !sample.IsLoaded() && !sample.IsDefault() &&
                     heap::GetStats(TAG_AUDIO).requestedBytes == audioBefore.requestedBytes && passed;
            sample.LoadResource(); // Destructor must also release a live sample.
        }
        passed = LedgerMatches(before) && passed;
    }
    Log(LogLevel::Info, "[D3BFG] CHECK audio/pcm-timing-amplitude-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

bool DefaultSamples()
{
    const heap::Stats before = heap::GetTotalStats();
    bool passed = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            idSoundSample sample;
            sample.SetName("_default");
            sample.LoadResource();
            sample.MakeDefault();
            passed = sample.IsLoaded() && sample.IsDefault() && sample.GetTimestamp() == FILE_NOT_FOUND_TIMESTAMP &&
                     sample.SampleRate() == 8000 && sample.NumSamples() == 256 && sample.NumChannels() == 1 &&
                     sample.BufferSize() == 512 && sample.LengthInMsec() == 32 && sample.GetAmplitude(0) == 1.0f &&
                     sample.GetAmplitude(31) == 1.0f && sample.GetAmplitude(32) == 0.0f && passed;
            sample.SetName("audio-fixtures/mono");
            sample.LoadResource();
            passed = !sample.IsDefault() && sample.LengthInMsec() == 1000 && passed;
            sample.MakeDefault();
            passed = sample.IsDefault() && idStr::Cmp(sample.GetName(), "audio-fixtures/mono") == 0 && passed;
        }
        passed = LedgerMatches(before) && passed;
    }
    Log(LogLevel::Info, "[D3BFG] CHECK audio/default-reload-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}
} // namespace

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
    const bool pcm = PcmSamples();
    const bool defaults = DefaultSamples();
    return passed && pcm && defaults;
}

bool RunAudioFailureProbe(const char * name)
{
    if (idStr::Cmp(name, "audio-resource") == 0)
    {
        idSoundSample sample;
        sample.SetName("audio-fixtures/missing");
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
    else if (idStr::Cmpn(name, "audio-wave-", 11) == 0)
    {
        const char * file = nullptr;
        if (idStr::Cmp(name, "audio-wave-format") == 0)
        {
            file = "audio-fixtures/unsupported";
        }
        else if (idStr::Cmp(name, "audio-wave-truncated") == 0)
        {
            file = "audio-fixtures/truncated";
        }
        else if (idStr::Cmp(name, "audio-wave-chunk") == 0)
        {
            file = "audio-fixtures/chunk";
        }
        else if (idStr::Cmp(name, "audio-wave-budget") == 0)
        {
            file = "audio-fixtures/budget";
        }
        else
        {
            return false;
        }
        idSoundSample sample;
        sample.SetName(file);
        sample.LoadResource();
    }
    else
    {
        return false;
    }
    return true; // The caller rejects an unavailable capability returning normally.
}
} // namespace ps2::smoketests
