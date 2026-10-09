// ================================================================================================
// File: sound_backend.h
// Brief: Keep Doom's logical sample/voice/device contracts independent of desktop audio APIs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

// Included after Doom's common headers and SoundVoice.h by sound/snd_local.h.
// Resource loading and playback are deferred capabilities; their implementations fail explicitly.
class idSoundSample final
{
  public:
    void LoadResource();
    void MakeDefault();
    void FreeData();

    void SetName(const char * name) { m_name = name; }
    const char * GetName() const { return m_name.c_str(); }
    // No loader can succeed yet; an unloaded sample is neither loaded nor a generated default.
    ID_TIME_T GetTimestamp() const { return FILE_NOT_FOUND_TIMESTAMP; }
    bool IsDefault() const { return false; }
    bool IsLoaded() const { return false; }

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
    idStr m_name;
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
