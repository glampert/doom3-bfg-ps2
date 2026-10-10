// ================================================================================================
// File: sound_backend.h
// Brief: Keep Doom's logical sample/voice/device contracts independent of desktop audio APIs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/audio/pcm_wave.h"

// Included after Doom's common headers and SoundVoice.h by sound/snd_local.h.
// Logical fixture samples own PCM; device/voice playback remains an explicit deferred capability.
class idSoundSample final
{
  public:
    idSoundSample() = default;
    ~idSoundSample();
    idSoundSample(const idSoundSample &) = delete;
    idSoundSample & operator=(const idSoundSample &) = delete;

    void LoadResource();
    void MakeDefault();
    void FreeData();

    void SetName(const char * name);
    const char * GetName() const { return m_name.c_str(); }
    ID_TIME_T GetTimestamp() const { return m_timestamp; }
    bool IsDefault() const { return m_isDefault; }
    bool IsLoaded() const { return m_pcm != nullptr; }

    int LengthInMsec() const;
    int SampleRate() const;
    int NumSamples() const;
    int NumChannels() const;
    int BufferSize() const;
    bool IsCompressed() const;
    float GetAmplitude(int timeMS) const;

    void SetNeverPurge() { m_neverPurge = true; }
    bool GetNeverPurge() const { return m_neverPurge; }
    void SetLevelLoadReferenced() { m_levelLoadReferenced = true; }
    void ResetLevelLoadReferenced() { m_levelLoadReferenced = false; }
    bool GetLevelLoadReferenced() const { return m_levelLoadReferenced; }
    int GetLastPlayedTime() const { return m_lastPlayedTime; }
    void SetLastPlayedTime(int time) { m_lastPlayedTime = time; }

  private:
    void RequireLoaded(const char * operation) const;
    idStr m_name;
    ps2::audio::PcmWave m_wave;
    unsigned char * m_pcm = nullptr;
    ID_TIME_T m_timestamp = FILE_NOT_FOUND_TIMESTAMP;
    bool m_isDefault = false;
    bool m_neverPurge = false;
    bool m_levelLoadReferenced = false;
    int m_lastPlayedTime = 0;
};

class idSoundVoice final : public idSoundVoice_Base
{
  public:
    void Start(int offsetMS, int flags);
    void Stop();
    void Pause();
    void UnPause();
    bool Update();
    float GetAmplitude();

  private:
    // Device ownership is required before any voice can exist.
    friend class idSoundHardware;
    idSoundVoice();
};

class idSoundHardware final
{
  public:
    void Init();
    void Shutdown();
    void Update();
    idSoundVoice * AllocateVoice(const idSoundSample * leadin, const idSoundSample * looping);
    void FreeVoice(idSoundVoice * voice);
    int GetNumZombieVoices() const { return 0; }
    int GetNumFreeVoices() const { return 0; }
};
