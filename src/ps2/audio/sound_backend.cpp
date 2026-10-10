// ================================================================================================
// File: sound_backend.cpp
// Brief: Own bounded logical PCM samples while keeping physical voice/device calls explicit.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

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

idSoundSample::~idSoundSample() { FreeData(); }

void idSoundSample::SetName(const char * name)
{
    if (name == nullptr)
    {
        ps2::FatalError("sound sample name is null");
    }
    FreeData();
    m_name = name;
}

void idSoundSample::LoadResource()
{
    FreeData();
    if (idStr::Icmpn(GetName(), "_default", 8) == 0)
    {
        MakeDefault();
        return;
    }
    if (!ps2::filesystem::IsRelativePath(GetName()))
    {
        ps2::FatalError("invalid sound sample path: %s", GetName());
    }
    if (!fileSystem->IsInitialized())
    {
        ps2::FatalError("sound sample loading requires filesystem startup");
    }
    // Native LoadSample supplies an extensionless canonical path. Avoid copying a whole WAV into ReadFile's buffer.
    char path[ps2::filesystem::kPathCapacity] = {};
    const int length = idStr::snPrintf(path, static_cast<int>(sizeof(path)), "%s.wav", GetName());
    if (length < 0 || static_cast<size_t>(length) >= sizeof(path))
    {
        ps2::FatalError("sound sample path is too long");
    }

    const char * error = nullptr;
    ps2::audio::PcmWave wave;
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
            const ps2::audio::WaveReader reader{ file, static_cast<size_t>(file->Length()), ReadAt };
            error = ps2::audio::InspectWave(reader, wave);
            if (error == nullptr)
            {
                pcm = static_cast<unsigned char *>(ps2::heap::TryAlloc(wave.dataBytes, TAG_AUDIO));
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
        ps2::heap::Free(pcm);
        ps2::FatalError("sound sample load failed: %s (%s)", GetName(), error);
    }
    m_wave = wave;
    m_pcm = pcm;
    m_timestamp = timestamp;
}

void idSoundSample::MakeDefault()
{
    FreeData();
    m_wave = { 0, 512, 8000, 1, 256 };
    m_pcm = static_cast<unsigned char *>(ps2::heap::Alloc(m_wave.dataBytes, TAG_AUDIO));
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

void idSoundSample::FreeData()
{
    ps2::heap::Free(m_pcm);
    m_pcm = nullptr;
    m_wave = {};
    m_timestamp = FILE_NOT_FOUND_TIMESTAMP;
    m_isDefault = false;
}

void idSoundSample::RequireLoaded(const char * operation) const
{
    if (!IsLoaded())
    {
        ps2::FatalError("sound sample query requires loaded data: %s", operation);
    }
}
int idSoundSample::LengthInMsec() const
{
    RequireLoaded("idSoundSample::LengthInMsec");
    return ps2::audio::DurationMsec(m_wave);
}
int idSoundSample::SampleRate() const
{
    RequireLoaded("idSoundSample::SampleRate");
    return static_cast<int>(m_wave.sampleRate);
}
int idSoundSample::NumSamples() const
{
    RequireLoaded("idSoundSample::NumSamples");
    return static_cast<int>(m_wave.frames);
}
int idSoundSample::NumChannels() const
{
    RequireLoaded("idSoundSample::NumChannels");
    return static_cast<int>(m_wave.channels);
}
int idSoundSample::BufferSize() const
{
    RequireLoaded("idSoundSample::BufferSize");
    return static_cast<int>(m_wave.dataBytes);
}
bool idSoundSample::IsCompressed() const
{
    RequireLoaded("idSoundSample::IsCompressed");
    return false;
}
float idSoundSample::GetAmplitude(int timeMS) const
{
    RequireLoaded("idSoundSample::GetAmplitude");
    return ps2::audio::PeakAmplitude(m_wave, m_pcm, timeMS);
}

void idSoundVoice::Start(int, int) { Unsupported("idSoundVoice::Start"); }
void idSoundVoice::Stop() { Unsupported("idSoundVoice::Stop"); }
void idSoundVoice::Pause() { Unsupported("idSoundVoice::Pause"); }
void idSoundVoice::UnPause() { Unsupported("idSoundVoice::UnPause"); }
bool idSoundVoice::Update() { Unsupported("idSoundVoice::Update"); }
float idSoundVoice::GetAmplitude() { Unsupported("idSoundVoice::GetAmplitude"); }

void idSoundHardware::Init() { Unsupported("idSoundHardware::Init"); }
void idSoundHardware::Shutdown() { /* Init cannot succeed, so no device or voices can need cleanup. */ }
void idSoundHardware::Update() { Unsupported("idSoundHardware::Update"); }
idSoundVoice * idSoundHardware::AllocateVoice(const idSoundSample *, const idSoundSample *)
{
    Unsupported("idSoundHardware::AllocateVoice");
}
void idSoundHardware::FreeVoice(idSoundVoice *) { Unsupported("idSoundHardware::FreeVoice"); }
