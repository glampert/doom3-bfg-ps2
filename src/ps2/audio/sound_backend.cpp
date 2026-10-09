// ================================================================================================
// File: sound_backend.cpp
// Brief: Make unavailable audio resource/device capabilities explicit without fabricating timing.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

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
} // namespace

void idSoundSample::LoadResource() { Unsupported("idSoundSample::LoadResource"); }
void idSoundSample::MakeDefault() { Unsupported("idSoundSample::MakeDefault"); }
void idSoundSample::FreeData() { /* No payload is owned until sample resource loading exists. */ }
int idSoundSample::LengthInMsec() const { Unsupported("idSoundSample::LengthInMsec"); }
int idSoundSample::SampleRate() const { Unsupported("idSoundSample::SampleRate"); }
int idSoundSample::NumSamples() const { Unsupported("idSoundSample::NumSamples"); }
int idSoundSample::NumChannels() const { Unsupported("idSoundSample::NumChannels"); }
int idSoundSample::BufferSize() const { Unsupported("idSoundSample::BufferSize"); }
bool idSoundSample::IsCompressed() const { Unsupported("idSoundSample::IsCompressed"); }
float idSoundSample::GetAmplitude(int) const { Unsupported("idSoundSample::GetAmplitude"); }

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
