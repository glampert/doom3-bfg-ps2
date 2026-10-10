// ================================================================================================
// File: pcm_wave_tests.cpp
// Brief: Exercise real RIFF boundaries, PCM math and corrupt inputs with the shared parser under sanitizers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/audio/pcm_wave.h"
#include "ps2/system/log.h"
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
struct MemoryReader
{
    const unsigned char * data;
    size_t size;
    size_t reads = 0;
    size_t failRead = 0;
    bool outOfBounds = false;
};

bool ReadAt(void * context, size_t offset, void * output, size_t size)
{
    auto & reader = *static_cast<MemoryReader *>(context);
    if (offset > reader.size || size > reader.size - offset)
    {
        reader.outOfBounds = true;
        return false;
    }
    if (++reader.reads == reader.failRead)
    {
        return false;
    }
    std::memcpy(output, reader.data + offset, size);
    return true;
}

void Put32(unsigned char * data, std::uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
    {
        data[i] = static_cast<unsigned char>(value >> (8 * i));
    }
}

std::array<unsigned char, 76> Fixture()
{
    std::array<unsigned char, 76> data = {};
    std::memcpy(data.data(), "RIFF", 4);
    Put32(data.data() + 4, 68);
    std::memcpy(data.data() + 8, "WAVEfmt ", 8);
    Put32(data.data() + 16, 16);
    data[20] = 1; // PCM.
    data[22] = 1; // Mono.
    Put32(data.data() + 24, 8000);
    Put32(data.data() + 28, 16000);
    data[32] = 2;
    data[34] = 16;
    std::memcpy(data.data() + 36, "data", 4);
    Put32(data.data() + 40, 32);
    data[45] = 128; // -32768, not a signed conversion or aligned short load.
    data[47] = 32;  // +8192.
    return data;
}

const char * Inspect(MemoryReader & reader, ps2::audio::PcmWave & wave)
{
    return ps2::audio::InspectWave({ &reader, reader.size, ReadAt }, wave);
}

bool Valid()
{
    auto data = Fixture();
    MemoryReader reader{ data.data(), data.size() };
    ps2::audio::PcmWave wave;
    bool passed = Inspect(reader, wave) == nullptr && !reader.outOfBounds && wave.frames == 16 &&
                  wave.channels == 1 && wave.sampleRate == 8000 && wave.dataOffset == 44 && wave.dataBytes == 32;
    passed = ps2::audio::DurationMsec(wave) == 2 &&
             ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, 0) == 1.0f &&
             ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, 1) == 1.0f &&
             ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, 2) == 0.0f &&
             ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, -1) == 0.0f &&
             ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, INT_MAX) == 0.0f && passed;

    // Legal data-before-format order, fmt's empty extension and an unknown odd-sized padded chunk.
    std::array<unsigned char, 90> reordered = {};
    std::memcpy(reordered.data(), data.data(), 12);
    Put32(reordered.data() + 4, 82);
    std::memcpy(reordered.data() + 12, data.data() + 36, 40);
    std::memcpy(reordered.data() + 52, "JUNK\x03\x00\x00\x00"
                                       "abc\x00",
                12);
    std::memcpy(reordered.data() + 64, data.data() + 12, 24);
    Put32(reordered.data() + 68, 18);
    reader = { reordered.data(), reordered.size() };
    passed = Inspect(reader, wave) == nullptr && wave.dataOffset == 20 && wave.frames == 16 &&
             !reader.outOfBounds && passed;
    return passed;
}

bool Rejected(const unsigned char * data, size_t size, const char * reason = nullptr, size_t failRead = 0)
{
    MemoryReader reader{ data, size, 0, failRead };
    ps2::audio::PcmWave wave{ 1, 2, 3, 4, 5 };
    const char * error = Inspect(reader, wave);
    return error != nullptr && (reason == nullptr || std::strcmp(error, reason) == 0) && !reader.outOfBounds &&
           wave.dataOffset == 0 && wave.dataBytes == 0 && wave.frames == 0 && wave.sampleRate == 0 && wave.channels == 0;
}

bool Malformed()
{
    const auto original = Fixture();
    bool passed = true;
    for (size_t size = 0; size < original.size(); ++size)
    {
        passed = Rejected(original.data(), size) && passed;
    }
    for (size_t read = 1; read <= 4; ++read)
    {
        passed = Rejected(original.data(), original.size(), nullptr, read) && passed;
    }
    // Independent corruptions of signature, PCM tag/channels/rate/byte rate/alignment/bit depth.
    for (size_t offset : { size_t{ 0 }, size_t{ 8 }, size_t{ 20 }, size_t{ 22 }, size_t{ 24 }, size_t{ 28 }, size_t{ 32 }, size_t{ 34 } })
    {
        auto data = original;
        data[offset] = 255;
        passed = Rejected(data.data(), data.size()) && passed;
    }
    auto data = original;
    Put32(data.data() + 40, 0xffffffffu);
    passed = Rejected(data.data(), data.size(), "chunk exceeds RIFF") && passed;
    data = original;
    std::memcpy(data.data() + 36, "fmt ", 4);
    passed = Rejected(data.data(), data.size(), "duplicate WAV format") && passed;
    data = original;
    Put32(data.data() + 40, 31);
    passed = Rejected(data.data(), data.size(), "incomplete PCM frames") && passed;
    data = original;
    Put32(data.data() + 40, 0);
    // Give empty data a complete, otherwise valid RIFF envelope.
    Put32(data.data() + 4, 36);
    passed = Rejected(data.data(), 44, "incomplete PCM frames") && passed;
    data = original;
    Put32(data.data() + 4, 38);
    Put32(data.data() + 40, 2);
    passed = Rejected(data.data(), 46, "PCM sample is shorter than one millisecond") && passed;
    data = original;
    std::memcpy(data.data() + 12, "JUNK", 4);
    passed = Rejected(data.data(), data.size(), "missing WAV format or data") && passed;
    data = original;
    std::memcpy(data.data() + 36, "JUNK", 4);
    passed = Rejected(data.data(), data.size(), "missing WAV format or data") && passed;
    // An odd final chunk without its mandatory pad byte cannot read beyond the RIFF envelope.
    data = original;
    Put32(data.data() + 4, 67);
    Put32(data.data() + 40, 31);
    passed = Rejected(data.data(), 75, "missing WAV chunk padding") && passed;
    return passed;
}

bool Mutations()
{
    std::uint32_t state = 0x12345678u;
    bool passed = true;
    for (size_t trial = 0; trial < 8192; ++trial)
    {
        auto data = Fixture();
        for (size_t change = 0; change < 1 + trial % 8; ++change)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            data[state % data.size()] ^= static_cast<unsigned char>(state >> 16);
        }
        MemoryReader reader{ data.data(), data.size() };
        ps2::audio::PcmWave wave;
        if (Inspect(reader, wave) == nullptr)
        {
            const float amplitude = ps2::audio::PeakAmplitude(wave, data.data() + wave.dataOffset, 0);
            passed = amplitude >= 0.0f && amplitude <= 1.0f && ps2::audio::DurationMsec(wave) > 0 && passed;
        }
        passed = !reader.outOfBounds && passed;
    }
    return passed;
}

bool StagedFixture(const char * path)
{
    std::array<unsigned char, ps2::audio::kMaxWaveBytes + 1> data;
    FILE * file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return false;
    }
    const size_t size = std::fread(data.data(), 1, data.size(), file);
    const bool read = std::ferror(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!read || !closed || size == data.size())
    {
        return false;
    }
    MemoryReader reader{ data.data(), size };
    ps2::audio::PcmWave wave;
    const char * error = Inspect(reader, wave);
    if (error != nullptr)
    {
        ps2::Log(ps2::LogLevel::Info, "rejected: %s\n", error);
        return !reader.outOfBounds;
    }
    ps2::Log(ps2::LogLevel::Info, "PCM rate=%u channels=%u frames=%u bytes=%zu duration=%d\n",
             static_cast<unsigned int>(wave.sampleRate), static_cast<unsigned int>(wave.channels),
             static_cast<unsigned int>(wave.frames), wave.dataBytes, ps2::audio::DurationMsec(wave));
    const unsigned char * pcm = data.data() + wave.dataOffset;
    if (wave.channels == 1)
    {
        return wave.sampleRate == 11025 && wave.frames == 11025 && wave.dataBytes == 22050 &&
               ps2::audio::DurationMsec(wave) == 1000 && ps2::audio::PeakAmplitude(wave, pcm, 0) == 1.0f &&
               ps2::audio::PeakAmplitude(wave, pcm, 17) == 0.25f && ps2::audio::PeakAmplitude(wave, pcm, 34) == 0.0f &&
               ps2::audio::PeakAmplitude(wave, pcm, 999) == 0.5f && ps2::audio::PeakAmplitude(wave, pcm, 1000) == 0.0f;
    }
    return wave.channels == 2 && wave.sampleRate == 44100 && wave.frames == 22050 && wave.dataBytes == 88200 &&
           ps2::audio::DurationMsec(wave) == 500 && ps2::audio::PeakAmplitude(wave, pcm, 0) == 0.5f &&
           ps2::audio::PeakAmplitude(wave, pcm, 499) == 0.5f && ps2::audio::PeakAmplitude(wave, pcm, 500) == 0.0f;
}
} // namespace

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        return 2;
    }
    bool passed = false;
    if (std::strcmp(argv[1], "valid") == 0)
    {
        passed = Valid();
    }
    else if (std::strcmp(argv[1], "malformed") == 0)
    {
        passed = Malformed();
    }
    else if (std::strcmp(argv[1], "mutations") == 0)
    {
        passed = Mutations();
    }
    else if (std::strcmp(argv[1], "fixture") == 0 && argc == 3)
    {
        passed = StagedFixture(argv[2]);
    }
    ps2::Log(ps2::LogLevel::Info, "PCM %s %s\n", argv[1], passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
