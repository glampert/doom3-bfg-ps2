// ================================================================================================
// File: sound_backend.h
// Brief: Keep Doom's logical sample/voice/device contracts independent of desktop audio APIs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/audio/pcm_wave.h"
#include "ps2/audio/voice_timeline.h"

// Included after Doom's common headers and SoundVoice.h by sound/snd_local.h.
// Logical samples/voices are usable with an explicit headless clock. Physical output remains deferred.
namespace ps2::audio
{

class SoundSample final
{
public:
    SoundSample() = default;
    ~SoundSample();

    SoundSample(const SoundSample &) = delete;
    SoundSample & operator=(const SoundSample &) = delete;

    void LoadResource();
    void MakeDefault();
    void FreeData();

    void SetName(const char * name);

    const char * GetName() const
    {
        return m_name.c_str();
    }

    ID_TIME_T GetTimestamp() const
    {
        return m_timestamp;
    }

    bool IsDefault() const
    {
        return m_isDefault;
    }

    bool IsLoaded() const
    {
        return m_pcm != nullptr;
    }

    int LengthInMsec() const;
    int SampleRate() const;
    int NumSamples() const;
    int NumChannels() const;
    int BufferSize() const;
    bool IsCompressed() const;
    float GetAmplitude(int timeMS) const;

    void SetNeverPurge()
    {
        m_neverPurge = true;
    }

    bool GetNeverPurge() const
    {
        return m_neverPurge;
    }

    void SetLevelLoadReferenced()
    {
        m_levelLoadReferenced = true;
    }

    void ResetLevelLoadReferenced()
    {
        m_levelLoadReferenced = false;
    }

    bool GetLevelLoadReferenced() const
    {
        return m_levelLoadReferenced;
    }

    int GetLastPlayedTime() const
    {
        return m_lastPlayedTime;
    }

    void SetLastPlayedTime(int time)
    {
        m_lastPlayedTime = time;
    }

private:
    friend class SoundVoice;
    void RetainVoice() const;
    void ReleaseVoice() const;
    void RequireLoaded(const char * operation) const;
    idStr m_name;
    PcmWave m_wave;
    unsigned char * m_pcm = nullptr;
    ID_TIME_T m_timestamp = FILE_NOT_FOUND_TIMESTAMP;
    bool m_isDefault = false;
    mutable unsigned int m_voiceReferences = 0;
    bool m_neverPurge = false;
    bool m_levelLoadReferenced = false;
    int m_lastPlayedTime = 0;
};

class SoundHardware;

class SoundVoice final : public idSoundVoice_Base
{
public:
    void Start(int offsetMS, int flags);
    void Stop();
    void Pause();
    void UnPause();
    bool Update();
    float GetAmplitude();
    void SetPitch(float value);
    const SoundSample * GetCurrentSample();
    PlaybackCursor GetPlaybackCursor();

private:
    // Pool ownership is required before any voice can exist; callers never delete voices.
    friend class SoundHardware;
    explicit SoundVoice(SoundHardware * owner);
    ~SoundVoice() = default;
    SoundVoice(const SoundVoice &) = delete;
    SoundVoice & operator=(const SoundVoice &) = delete;
    void Bind(const SoundSample * leadin, const SoundSample * looping);
    void Release();
    void RequireAllocated() const;
    void Sync();
    SoundHardware * m_owner;
    const SoundSample * m_leadin = nullptr;
    const SoundSample * m_looping = nullptr;
    VoiceTimeline m_timeline;
    int m_flags = 0;
    bool m_allocated = false;
};

class SoundHardware final
{
public:
    static constexpr int kHeadlessVoiceCount = MAX_HARDWARE_VOICES;
    SoundHardware() = default;
    ~SoundHardware();

    SoundHardware(const SoundHardware &) = delete;
    SoundHardware & operator=(const SoundHardware &) = delete;

    // Select logical playback explicitly. Clock context must remain valid while the pool is used.
    // Shutdown releases resources; Init can recreate the pool with the same selected clock.
    void InitHeadless(VoiceClock clock);

    bool IsHeadlessInitialized() const
    {
        return m_voices != nullptr;
    }

    void Init();
    void Shutdown();
    void Update();
    SoundVoice * AllocateVoice(const SoundSample * leadin, const SoundSample * looping);
    void FreeVoice(SoundVoice * voice);

    int GetNumZombieVoices() const
    {
        return 0;
    }

    int GetNumFreeVoices() const;

private:
    friend class SoundVoice;
    std::uint64_t Now();
    void RequireInitialized(const char * operation) const;
    SoundVoice * m_voices = nullptr;
    VoiceClock m_clock;
    std::uint64_t m_lastTime = 0;
    bool m_haveClock = false;
};

} // namespace ps2::audio

// The engine names the logical backend through its original classes; sound.h aliases idSoundSample.
using idSoundVoice = ps2::audio::SoundVoice;
using idSoundHardware = ps2::audio::SoundHardware;
