// ================================================================================================
// File: sound_backend.cpp
// Brief: Own bounded logical PCM samples while keeping physical voice/device calls explicit.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <new>

// snd_local.h supplies the native logical sound and portable backend declarations.
#include <sound/snd_local.h>

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * operation)
{
    ps2::FatalError("audio capability unavailable: %s", operation);
}

bool ReadAt(void * context, size_t offset, void * output, size_t size)
{
    auto * file = static_cast<idFile *>(context);
    PS2_Assert(file != nullptr);
    return offset <= INT_MAX && size <= INT_MAX && file->Seek(static_cast<long>(offset), FS_SEEK_SET) == 0 &&
           file->Read(output, static_cast<int>(size)) == static_cast<int>(size);
}
} // namespace

// Preserve native control defaults without importing the desktop surround implementation or its cvars.
idSoundVoice_Base::idSoundVoice_Base()
    : position(0.0f), gain(1.0f), centerChannel(0.0f), pitch(1.0f), innerRadius(32.0f), occlusion(0.0f), channelMask(0), innerSampleRangeSqr(0.0f), outerSampleRangeSqr(0.0f)
{
}

void idSoundVoice_Base::InitSurround(int, int)
{
    Unsupported("idSoundVoice_Base::InitSurround");
}

void idSoundVoice_Base::CalculateSurround(int, float *, float)
{
    Unsupported("idSoundVoice_Base::CalculateSurround");
}

const idSoundSample * idSoundVoice_Base::GetCurrentSample()
{
    Unsupported("idSoundVoice_Base::GetCurrentSample");
}

namespace ps2::audio
{

SoundSample::~SoundSample()
{
    FreeData();
}

void SoundSample::SetName(const char * name)
{
    if (name == nullptr)
    {
        FatalError("sound sample name is null");
    }
    FreeData();
    m_name = name;
}

void SoundSample::LoadResource()
{
    FreeData();
    if (idStr::Icmpn(GetName(), "_default", 8) == 0)
    {
        MakeDefault();
        return;
    }
    if (!system::filesystem::IsRelativePath(GetName()))
    {
        FatalError("invalid sound sample path: %s", GetName());
    }
    if (!fileSystem->IsInitialized())
    {
        FatalError("sound sample loading requires filesystem startup");
    }
    // Native LoadSample supplies an extensionless canonical path. Avoid copying a whole WAV into ReadFile's buffer.
    char path[system::filesystem::kPathCapacity] = {};
    const int length = idStr::snPrintf(path, static_cast<int>(sizeof(path)), "%s.wav", GetName());
    if (length < 0 || static_cast<size_t>(length) >= sizeof(path))
    {
        FatalError("sound sample path is too long");
    }

    const char * error = nullptr;
    PcmWave wave;
    unsigned char * pcm = nullptr;
    ID_TIME_T timestamp = FILE_NOT_FOUND_TIMESTAMP;
    {
        idFileLocal file(fileSystem->OpenFileRead(path, false, nullptr));
        if (file == nullptr)
        {
            error = "WAV file not found";
        }
        else if (file->Length() < 0)
        {
            error = "invalid WAV file length";
        }
        else
        {
            const WaveReader reader{ file, static_cast<size_t>(file->Length()), ReadAt };
            error = InspectWave(reader, wave);
            if (error == nullptr)
            {
                pcm = static_cast<unsigned char *>(system::heap::TryAlloc(wave.dataBytes, TAG_AUDIO));
                if (pcm == nullptr)
                {
                    error = "PCM allocation failed";
                }
                else if (!ReadAt(file, wave.dataOffset, pcm, wave.dataBytes))
                {
                    error = "PCM payload read failed";
                }
                else
                {
                    timestamp = file->Timestamp();
                }
            }
        }
    } // Close the stream before reporting fatal failures; failed loads retain no temporary payload.
    if (error != nullptr)
    {
        system::heap::Free(pcm);
        FatalError("sound sample load failed: %s (%s)", GetName(), error);
    }
    m_wave = wave;
    m_pcm = pcm;
    m_timestamp = timestamp;
}

void SoundSample::MakeDefault()
{
    FreeData();
    m_wave = { 0, 512, 8000, 1, 256 };
    m_pcm = static_cast<unsigned char *>(system::heap::Alloc(m_wave.dataBytes, TAG_AUDIO));
    // A real short square-wave sample; the same timing/amplitude path handles loaded and generated data.
    for (size_t i = 0; i < m_wave.dataBytes; i += 4)
    {
        m_pcm[i] = 0;
        m_pcm[i + 1] = 128;
        m_pcm[i + 2] = 255;
        m_pcm[i + 3] = 127;
    }
    m_isDefault = true;
}

void SoundSample::FreeData()
{
    if (m_voiceReferences != 0)
    {
        FatalError("sound sample still referenced by logical voices: %s", GetName());
    }
    system::heap::Free(m_pcm);
    m_pcm = nullptr;
    m_wave = {};
    m_timestamp = FILE_NOT_FOUND_TIMESTAMP;
    m_isDefault = false;
}

void SoundSample::RetainVoice() const
{
    RequireLoaded("logical voice allocation");
    if (m_voiceReferences == UINT_MAX)
    {
        FatalError("logical voice sample reference overflow");
    }
    ++m_voiceReferences;
}

void SoundSample::ReleaseVoice() const
{
    if (m_voiceReferences == 0)
    {
        FatalError("logical voice sample reference underflow");
    }
    --m_voiceReferences;
}

void SoundSample::RequireLoaded(const char * operation) const
{
    if (!IsLoaded())
    {
        FatalError("sound sample query requires loaded data: %s", operation);
    }
}

int SoundSample::LengthInMsec() const
{
    RequireLoaded("SoundSample::LengthInMsec");
    return DurationMsec(m_wave);
}

int SoundSample::SampleRate() const
{
    RequireLoaded("SoundSample::SampleRate");
    return static_cast<int>(m_wave.sampleRate);
}

int SoundSample::NumSamples() const
{
    RequireLoaded("SoundSample::NumSamples");
    return static_cast<int>(m_wave.frames);
}

int SoundSample::NumChannels() const
{
    RequireLoaded("SoundSample::NumChannels");
    return static_cast<int>(m_wave.channels);
}

int SoundSample::BufferSize() const
{
    RequireLoaded("SoundSample::BufferSize");
    return static_cast<int>(m_wave.dataBytes);
}

bool SoundSample::IsCompressed() const
{
    RequireLoaded("SoundSample::IsCompressed");
    return false;
}

float SoundSample::GetAmplitude(int timeMS) const
{
    RequireLoaded("SoundSample::GetAmplitude");
    return PeakAmplitude(m_wave, m_pcm, timeMS);
}

SoundVoice::SoundVoice(SoundHardware * owner) : m_owner(owner)
{
    PS2_Assert(owner != nullptr);
}

void SoundVoice::RequireAllocated() const
{
    if (!m_allocated)
    {
        FatalError("logical voice is not allocated");
    }
}

void SoundVoice::Bind(const SoundSample * leadin, const SoundSample * looping)
{
    PS2_Assert(!m_allocated && leadin != nullptr);
    const SampleTiming leadinTime{ leadin->m_wave.frames, leadin->m_wave.sampleRate };
    const SampleTiming loopTime = looping == nullptr ? SampleTiming{} : SampleTiming{ looping->m_wave.frames, looping->m_wave.sampleRate };
    m_timeline.Configure(leadinTime, loopTime);
    leadin->RetainVoice();
    if (looping != nullptr)
    {
        looping->RetainVoice();
    }
    m_leadin = leadin;
    m_looping = looping;
    m_flags = 0;
    m_allocated = true;
    // Every reuse starts with native defaults, even if the previous owner changed pitch or spatial controls.
    position.Zero();
    gain = 1.0f;
    centerChannel = 0.0f;
    pitch = 1.0f;
    innerRadius = 32.0f;
    occlusion = 0.0f;
    channelMask = 0;
    innerSampleRangeSqr = outerSampleRangeSqr = 0.0f;
}

void SoundVoice::Release()
{
    RequireAllocated();
    m_leadin->ReleaseVoice();
    if (m_looping != nullptr)
    {
        m_looping->ReleaseVoice();
    }
    m_leadin = m_looping = nullptr;
    m_timeline = {};
    m_flags = 0;
    m_allocated = false;
}

void SoundVoice::Sync()
{
    RequireAllocated();
    m_timeline.Advance(m_owner->Now());
}

void SoundVoice::Start(int offsetMS, int flags)
{
    RequireAllocated();
    static constexpr int kNativeFlags = SSF_PRIVATE_SOUND | SSF_ANTI_PRIVATE_SOUND | SSF_NO_OCCLUSION | SSF_GLOBAL |
                                        SSF_OMNIDIRECTIONAL | SSF_LOOPING | SSF_PLAY_ONCE | SSF_UNCLAMPED | SSF_NO_FLICKER | SSF_NO_DUPS | SSF_VO | SSF_MUSIC;
    if ((flags & ~kNativeFlags) != 0)
    {
        FatalError("logical voice received unknown sound flags");
    }
    m_timeline.Start(offsetMS, m_owner->Now());
    m_flags = flags;
}

void SoundVoice::Stop()
{
    RequireAllocated();
    m_timeline.Stop(m_owner->Now());
}

void SoundVoice::Pause()
{
    RequireAllocated();
    m_timeline.Pause(m_owner->Now());
}

void SoundVoice::UnPause()
{
    RequireAllocated();
    m_timeline.UnPause(m_owner->Now());
}

bool SoundVoice::Update()
{
    Sync();
    const auto state = m_timeline.GetCursor().state;
    return state == VoiceState::Playing || state == VoiceState::Paused;
}

void SoundVoice::SetPitch(float value)
{
    RequireAllocated();
    m_timeline.SetPitch(value, m_owner->Now());
    pitch = m_timeline.GetPitch();
}

PlaybackCursor SoundVoice::GetPlaybackCursor()
{
    Sync();
    return m_timeline.GetCursor();
}

const SoundSample * SoundVoice::GetCurrentSample()
{
    const auto segment = GetPlaybackCursor().segment;
    if (segment == VoiceSegment::None)
    {
        return nullptr;
    }
    return segment == VoiceSegment::Leadin ? m_leadin : m_looping;
}

float SoundVoice::GetAmplitude()
{
    const auto cursor = GetPlaybackCursor();
    if (cursor.state != VoiceState::Playing)
    {
        return 0.0f;
    }
    if ((m_flags & SSF_NO_FLICKER) != 0)
    {
        return 1.0f;
    }
    const SoundSample * sample = cursor.segment == VoiceSegment::Leadin ? m_leadin : m_looping;
    return sample->GetAmplitude(cursor.timeMS); // Pre-gain envelope; native callers apply GetGain separately.
}

SoundHardware::~SoundHardware()
{
    Shutdown();
}

void SoundHardware::RequireInitialized(const char * operation) const
{
    if (m_voices == nullptr)
    {
        Unsupported(operation);
    }
}

std::uint64_t SoundHardware::Now()
{
    PS2_Assert(m_clock.nowUsec != nullptr);
    const std::uint64_t now = m_clock.nowUsec(m_clock.context);
    if (m_haveClock && now < m_lastTime)
    {
        FatalError("logical audio clock moved backwards");
    }
    m_haveClock = true;
    m_lastTime = now;
    return now;
}

void SoundHardware::InitHeadless(VoiceClock clock)
{
    if (m_voices != nullptr)
    {
        FatalError("logical audio clock cannot change while initialized");
    }
    if (clock.nowUsec == nullptr)
    {
        FatalError("logical audio requires a clock callback");
    }
    m_clock = clock;
    Init();
}

void SoundHardware::Init()
{
    if (m_clock.nowUsec == nullptr)
    {
        Unsupported("SoundHardware::Init");
    }
    if (m_voices != nullptr)
    {
        return;
    }
    m_haveClock = false;
    (void)Now();
    m_voices = static_cast<SoundVoice *>(system::heap::Alloc(sizeof(SoundVoice) * kHeadlessVoiceCount, TAG_AUDIO));
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        ::new (static_cast<void *>(m_voices + i)) SoundVoice(this);
    }
}

void SoundHardware::Shutdown()
{
    if (m_voices == nullptr)
    {
        return;
    }
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        if (m_voices[i].m_allocated)
        {
            m_voices[i].Release();
        }
        m_voices[i].~SoundVoice();
    }
    system::heap::Free(m_voices);
    m_voices = nullptr;
    m_haveClock = false;
}

void SoundHardware::Update()
{
    RequireInitialized("SoundHardware::Update");
    const std::uint64_t now = Now();
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        if (m_voices[i].m_allocated)
        {
            m_voices[i].m_timeline.Advance(now);
        }
    }
}

SoundVoice * SoundHardware::AllocateVoice(const SoundSample * leadin, const SoundSample * looping)
{
    RequireInitialized("SoundHardware::AllocateVoice");
    if (leadin == nullptr)
    {
        return nullptr;
    } // Preserve the native allocator's absent-sample result.
    if (!leadin->IsLoaded() || (looping != nullptr && !looping->IsLoaded()))
    {
        FatalError("logical voice allocation requires loaded samples");
    }
    if (looping != nullptr && leadin->NumChannels() != looping->NumChannels())
    {
        FatalError("logical voice samples require matching channels");
    }
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        if (!m_voices[i].m_allocated)
        {
            m_voices[i].Bind(leadin, looping);
            return m_voices + i;
        }
    }
    return nullptr;
}

void SoundHardware::FreeVoice(SoundVoice * voice)
{
    RequireInitialized("SoundHardware::FreeVoice");
    // Compare slot addresses before dereferencing an untrusted/foreign voice pointer.
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        if (voice == m_voices + i)
        {
            voice->Release(); // Synchronous logical stop: no DMA ownership or zombie interval.
            return;
        }
    }
    FatalError("logical voice does not belong to this pool");
}

int SoundHardware::GetNumFreeVoices() const
{
    if (m_voices == nullptr)
    {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < kHeadlessVoiceCount; ++i)
    {
        if (!m_voices[i].m_allocated)
        {
            ++count;
        }
    }
    return count;
}

} // namespace ps2::audio
