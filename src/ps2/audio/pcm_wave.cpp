// ================================================================================================
// File: pcm_wave.cpp
// Brief: Reject corrupt RIFF lengths/formats before allocation and keep host/EE sample math identical.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/audio/pcm_wave.h"
#include "ps2/common.h"
#include <cstring>

namespace
{
std::uint32_t Little16(const unsigned char * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8);
}

std::uint32_t Little32(const unsigned char * bytes)
{
    return Little16(bytes) | (Little16(bytes + 2) << 16);
}

bool ValidPcm(const ps2::audio::PcmWave & wave)
{
    return wave.sampleRate >= 8000 && wave.sampleRate <= 48000 && wave.channels >= 1 && wave.channels <= 2 &&
           wave.frames > 0 && wave.frames <= ps2::audio::kMaxPcmBytes / (wave.channels * 2) &&
           wave.dataBytes == static_cast<size_t>(wave.frames) * wave.channels * 2 &&
           static_cast<std::uint64_t>(wave.frames) * 1000 >= wave.sampleRate;
}
} // namespace

namespace ps2::audio
{
const char * InspectWave(const WaveReader & reader, PcmWave & output)
{
    output = {};
    if (reader.readAt == nullptr || reader.length < 12)
    {
        return "invalid RIFF header";
    }
    if (reader.length > kMaxWaveBytes)
    {
        return "WAV container exceeds fixture budget";
    }
    unsigned char header[18] = {};
    if (!reader.readAt(reader.context, 0, header, 12))
    {
        return "WAV header read failed";
    }
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
    {
        return "invalid RIFF header";
    }
    if (static_cast<size_t>(Little32(header + 4)) != reader.length - 8)
    {
        return "RIFF length mismatch";
    }

    PcmWave wave;
    bool haveFormat = false;
    bool haveData = false;
    size_t offset = 12;
    size_t chunks = 0;
    while (offset < reader.length)
    {
        if (++chunks > 256)
        {
            return "too many WAV chunks";
        }
        if (reader.length - offset < 8)
        {
            return "truncated WAV chunk header";
        }
        if (!reader.readAt(reader.context, offset, header, 8))
        {
            return "WAV chunk read failed";
        }
        const size_t size = Little32(header + 4);
        offset += 8;
        if (size > reader.length - offset)
        {
            return "chunk exceeds RIFF";
        }
        const size_t padded = size + (size & 1);
        if (padded > reader.length - offset)
        {
            return "missing WAV chunk padding";
        }

        if (std::memcmp(header, "fmt ", 4) == 0)
        {
            if (haveFormat)
            {
                return "duplicate WAV format";
            }
            if (size != 16 && size != 18)
            {
                return "unsupported PCM format";
            }
            if (!reader.readAt(reader.context, offset, header, size))
            {
                return "WAV format read failed";
            }
            wave.channels = Little16(header + 2);
            wave.sampleRate = Little32(header + 4);
            if (Little16(header) != 1 || Little16(header + 14) != 16 ||
                (size == 18 && Little16(header + 16) != 0))
            {
                return "unsupported PCM format";
            }
            if (wave.channels < 1 || wave.channels > 2 || wave.sampleRate < 8000 || wave.sampleRate > 48000)
            {
                return "unsupported PCM rate or channels";
            }
            if (Little16(header + 12) != wave.channels * 2 ||
                Little32(header + 8) != wave.sampleRate * wave.channels * 2)
            {
                return "inconsistent PCM frame layout";
            }
            haveFormat = true;
        }
        else if (std::memcmp(header, "data", 4) == 0)
        {
            if (haveData)
            {
                return "duplicate WAV data";
            }
            if (size > kMaxPcmBytes)
            {
                return "PCM data exceeds fixture budget";
            }
            wave.dataOffset = offset;
            wave.dataBytes = size;
            haveData = true;
        }
        offset += padded;
    }
    if (!haveFormat || !haveData)
    {
        return "missing WAV format or data";
    }
    if (wave.dataBytes == 0 || wave.dataBytes % (wave.channels * 2) != 0)
    {
        return "incomplete PCM frames";
    }
    wave.frames = static_cast<std::uint32_t>(wave.dataBytes / (wave.channels * 2));
    if (static_cast<std::uint64_t>(wave.frames) * 1000 < wave.sampleRate)
    {
        return "PCM sample is shorter than one millisecond";
    }
    output = wave;
    return nullptr;
}

int DurationMsec(const PcmWave & wave)
{
    if (!ValidPcm(wave))
    {
        FatalError("invalid logical PCM metadata");
    }
    return static_cast<int>(static_cast<std::uint64_t>(wave.frames) * 1000 / wave.sampleRate);
}

float PeakAmplitude(const PcmWave & wave, const unsigned char * pcm, int timeMS)
{
    if (!ValidPcm(wave) || pcm == nullptr)
    {
        FatalError("invalid logical PCM payload");
    }
    if (timeMS < 0 || static_cast<std::uint64_t>(timeMS) * wave.sampleRate / 1000 >= wave.frames)
    {
        return 0.0f;
    }
    const std::uint64_t window = static_cast<std::uint64_t>(timeMS) * 60 / 1000;
    const size_t first = static_cast<size_t>(window * wave.sampleRate / 60) * wave.channels;
    const size_t endFrame = static_cast<size_t>((window + 1) * wave.sampleRate / 60);
    const size_t last = (endFrame < wave.frames ? endFrame : wave.frames) * wave.channels;
    std::uint32_t peak = 0;
    for (size_t i = first; i < last; ++i)
    {
        const std::uint32_t raw = Little16(pcm + i * 2);
        const std::uint32_t magnitude = raw >= 32768 ? 65536 - raw : raw;
        if (magnitude > peak)
        {
            peak = magnitude;
        }
    }
    return static_cast<float>(peak) / 32768.0f;
}
} // namespace ps2::audio
