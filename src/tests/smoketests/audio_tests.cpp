// ================================================================================================
// File: audio_tests.cpp
// Brief: Check real PCM timing/amplitude and exact repeated sample ownership without an audio device.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/audio_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "tests/smoketests/voice_tests.h"
#include <idlib/precompiled.h>

// This includes the real logical sound declarations and the portable backend seam.
#include <sound/snd_local.h>

namespace ps2::smoketests
{
namespace
{
std::uint64_t FixtureTime(void * context)
{
    PS2_Assert(context != nullptr);
    return *static_cast<std::uint64_t *>(context);
}

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

bool VoicePlayback()
{
    const heap::Stats before = heap::GetTotalStats();
    bool passed = true;
    {
        std::uint64_t now = 0;
        idSoundSample sample;
        sample.SetName("audio-fixtures/mono");
        sample.LoadResource();
        idSoundHardware hardware;
        hardware.InitHeadless({ &now, FixtureTime });
        hardware.Init(); // Native Init is idempotent after explicit logical-mode selection.
        idSoundVoice * voice = hardware.AllocateVoice(&sample, nullptr);
        if (voice == nullptr)
        {
            return false;
        }
        passed = voice->GetGain() == 1.0f && voice->GetPitch() == 1.0f && voice->GetCurrentSample() == nullptr &&
                 voice->GetAmplitude() == 0.0f && !voice->Update();
        voice->Start(0, 0);
        passed = voice->GetAmplitude() == 1.0f && voice->GetCurrentSample() == &sample && passed;
        now = 17000;
        passed = voice->GetAmplitude() == 0.25f && voice->GetPlaybackCursor().frame == 187 && passed;
        voice->Pause();
        now = 1000000;
        hardware.Update();
        passed = voice->GetAmplitude() == 0.0f && voice->GetPlaybackCursor().frame == 187 &&
                 voice->GetPlaybackCursor().state == audio::VoiceState::Paused && voice->Update() && passed;
        voice->UnPause();
        now += 17000;
        passed = voice->GetAmplitude() == 0.0f && voice->GetPlaybackCursor().timeMS == 34 && passed;
        voice->SetPitch(2.0f);
        now += 10000;
        passed = voice->GetPlaybackCursor().timeMS == 54 && voice->GetPitch() == 2.0f && passed;
        voice->SetPitch(0.0f);
        now += 10000;
        passed = voice->GetPlaybackCursor().timeMS == 54 && passed;
        voice->Stop();
        voice->UnPause();
        passed = !voice->Update() && voice->GetAmplitude() == 0.0f && voice->GetCurrentSample() == nullptr && passed;
        voice->Start(1000, 0);
        passed = !voice->Update() && voice->GetPlaybackCursor().state == audio::VoiceState::Complete && passed;
        hardware.FreeVoice(voice);
        sample.FreeData(); // A live voice would reject this purge.
        passed = hardware.GetNumFreeVoices() == idSoundHardware::kHeadlessVoiceCount && passed;
    }
    passed = LedgerMatches(before) && passed;
    Log(LogLevel::Info, "[D3BFG] CHECK audio/voice-playback-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

bool VoiceLoops()
{
    const heap::Stats before = heap::GetTotalStats();
    bool passed = true;
    {
        std::uint64_t now = 0;
        idSoundSample leadin, loop;
        leadin.MakeDefault(); // 32 ms at 8 kHz.
        loop.SetName("audio-fixtures/mono");
        loop.LoadResource(); // 1 s at 11,025 Hz, same channel layout.
        idSoundHardware hardware;
        hardware.InitHeadless({ &now, FixtureTime });
        idSoundVoice * voice = hardware.AllocateVoice(&leadin, &loop);
        if (voice == nullptr)
        {
            return false;
        }
        voice->Start(1032, SSF_LOOPING);
        passed = voice->GetCurrentSample() == &loop && voice->GetPlaybackCursor().frame == 0 &&
                 voice->GetAmplitude() == 1.0f;
        voice->Start(49, SSF_LOOPING);
        passed = voice->GetPlaybackCursor().timeMS == 17 && voice->GetAmplitude() == 0.25f && passed;
        voice->Start(66, SSF_LOOPING | SSF_NO_FLICKER);
        voice->SetGain(0.5f);
        passed = voice->GetPlaybackCursor().timeMS == 34 && voice->GetAmplitude() == 1.0f &&
                 voice->GetGain() == 0.5f && passed;
        voice->SetPitch(2.0f);
        now = 500000;
        hardware.Update();
        passed = voice->GetPlaybackCursor().timeMS == 34 && voice->GetCurrentSample() == &loop && passed;
        voice->Pause();
        passed = voice->GetAmplitude() == 0.0f && passed;
        hardware.FreeVoice(voice);
    }
    passed = LedgerMatches(before) && passed;
    Log(LogLevel::Info, "[D3BFG] CHECK audio/voice-loop-envelope-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

bool VoicePool()
{
    const heap::Stats before = heap::GetTotalStats();
    bool passed = true;
    size_t poolBytes = 0;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            std::uint64_t now = 0;
            idSoundSample sample;
            sample.MakeDefault();
            const heap::Stats withSample = heap::GetTotalStats();
            idSoundHardware hardware;
            passed = !hardware.IsHeadlessInitialized() && hardware.GetNumFreeVoices() == 0 && passed;
            hardware.InitHeadless({ &now, FixtureTime });
            const heap::Stats withPool = heap::GetTotalStats();
            poolBytes = sizeof(idSoundVoice) * idSoundHardware::kHeadlessVoiceCount;
            passed = withPool.requestedBytes == withSample.requestedBytes + poolBytes &&
                     withPool.allocationCount == withSample.allocationCount + 1 && passed;
            idSoundVoice * voices[idSoundHardware::kHeadlessVoiceCount] = {};
            for (int i = 0; i < idSoundHardware::kHeadlessVoiceCount; ++i)
            {
                voices[i] = hardware.AllocateVoice(&sample, &sample);
                if (voices[i] == nullptr)
                {
                    return false;
                }
                voices[i]->Start(0, SSF_LOOPING);
            }
            passed = hardware.IsHeadlessInitialized() && hardware.GetNumFreeVoices() == 0 &&
                     hardware.GetNumZombieVoices() == 0 && hardware.AllocateVoice(&sample, nullptr) == nullptr &&
                     hardware.AllocateVoice(nullptr, nullptr) == nullptr && passed;
            voices[3]->SetPitch(0.0f);
            voices[3]->SetGain(2.0f);
            hardware.FreeVoice(voices[3]);
            hardware.FreeVoice(voices[7]);
            passed = hardware.GetNumFreeVoices() == 2 && passed;
            idSoundVoice * reused = hardware.AllocateVoice(&sample, nullptr);
            if (reused == nullptr)
            {
                return false;
            }
            passed = reused == voices[3] && reused->GetPitch() == 1.0f && reused->GetGain() == 1.0f &&
                     reused->GetAmplitude() == 0.0f && !reused->Update() && passed;
            hardware.Shutdown(); // Also releases every still-owned sample reference, including self-loops.
            hardware.Shutdown();
            passed = !hardware.IsHeadlessInitialized() && LedgerMatches(withSample) && passed;
            sample.FreeData();
            sample.MakeDefault();
            hardware.Init(); // The explicit logical clock selection survives resource shutdown/restart.
            idSoundVoice * restarted = hardware.AllocateVoice(&sample, nullptr);
            if (restarted == nullptr)
            {
                return false;
            }
            restarted->Start(0, 0); // Destructor must release a live pool before the sample dies.
        }
        passed = LedgerMatches(before) && passed;
    }
    Log(LogLevel::Info, "[D3BFG] LOGICAL_AUDIO pool_bytes=%zu voices=%d\n", poolBytes, idSoundHardware::kHeadlessVoiceCount);
    Log(LogLevel::Info, "[D3BFG] CHECK audio/voice-pool-ledger %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

bool VoiceFailureProbe(const char * name)
{
    if (idStr::Cmpn(name, "audio-voice-", 12) != 0)
    {
        return false;
    }
    std::uint64_t now = 100;
    idSoundSample sample;
    sample.SetName("_default-pin");
    idSoundHardware hardware;
    if (idStr::Cmp(name, "audio-voice-clock") == 0)
    {
        hardware.InitHeadless({});
        return true;
    }
    hardware.InitHeadless({ &now, FixtureTime });
    if (idStr::Cmp(name, "audio-voice-backwards") == 0)
    {
        now = 99;
        hardware.Update();
        return true;
    }
    if (idStr::Cmp(name, "audio-voice-unloaded") == 0)
    {
        (void)hardware.AllocateVoice(&sample, nullptr);
        return true;
    }
    sample.MakeDefault();
    if (idStr::Cmp(name, "audio-voice-format") == 0)
    {
        idSoundSample stereo;
        stereo.SetName("audio-fixtures/stereo");
        stereo.LoadResource();
        (void)hardware.AllocateVoice(&sample, &stereo);
        return true;
    }
    idSoundVoice * voice = hardware.AllocateVoice(&sample, nullptr);
    if (voice == nullptr)
    {
        FatalError("voice probe could not allocate its fixture");
    }
    if (idStr::Cmp(name, "audio-voice-offset") == 0)
    {
        voice->Start(-1, 0);
    }
    else if (idStr::Cmp(name, "audio-voice-pitch") == 0)
    {
        voice->SetPitch(-1.0f);
    }
    else if (idStr::Cmp(name, "audio-voice-pinned") == 0)
    {
        sample.FreeData();
    }
    else if (idStr::Cmp(name, "audio-voice-flags") == 0)
    {
        voice->Start(0, 1 << 20);
    }
    else if (idStr::Cmp(name, "audio-voice-double-free") == 0)
    {
        hardware.FreeVoice(voice);
        hardware.FreeVoice(voice);
    }
    else if (idStr::Cmp(name, "audio-voice-foreign") == 0)
    {
        idSoundHardware other;
        other.InitHeadless({ &now, FixtureTime });
        other.FreeVoice(voice);
    }
    else
    {
        return false;
    }
    return true;
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
    const bool timeline = RunVoiceTimelineTests();
    const bool playback = VoicePlayback();
    const bool loops = VoiceLoops();
    const bool pool = VoicePool();
    return passed && pcm && defaults && timeline && playback && loops && pool;
}

bool RunAudioFailureProbe(const char * name)
{
    if (VoiceFailureProbe(name))
    {
        return true;
    }
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
