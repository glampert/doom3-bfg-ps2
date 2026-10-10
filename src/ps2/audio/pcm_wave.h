// ================================================================================================
// File: pcm_wave.h
// Brief: Validate bounded fixture WAV streams and derive logical timing from real PCM frames.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::audio
{
// Initial headless fixture limits, not a retail sound-cache or streaming budget.
static constexpr size_t kMaxPcmBytes = 256 * 1024;
static constexpr size_t kMaxWaveBytes = kMaxPcmBytes + 65536;

struct WaveReader
{
    void * context;
    size_t length;
    bool (*readAt)(void *, size_t offset, void * output, size_t size);
};

struct PcmWave
{
    size_t dataOffset = 0;
    size_t dataBytes = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t frames = 0; // Per channel, matching idSoundSample::NumSamples.
};

// Returns a static diagnostic on failure and clears output. Scans headers without copying the file.
const char * InspectWave(const WaveReader & reader, PcmWave & output);
// Require an inspected wave (or generated PCM with the same invariants).
int DurationMsec(const PcmWave & wave);
// Peak absolute PCM value in the containing 1/60-second window, over all channels.
// Times outside the frame range return zero; PCM bytes remain little-endian on both host and EE.
float PeakAmplitude(const PcmWave & wave, const unsigned char * pcm, int timeMS);
} // namespace ps2::audio
