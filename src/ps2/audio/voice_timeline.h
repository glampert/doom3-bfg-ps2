// ================================================================================================
// File: voice_timeline.h
// Brief: Advance logical playback with an explicit clock, independently of an output device.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

namespace ps2::audio
{
struct VoiceClock
{
    void * context = nullptr;
    std::uint64_t (*nowUsec)(void *) = nullptr; // Monotonic within an initialized device lifetime.
};

struct SampleTiming
{
    std::uint32_t frames = 0;
    std::uint32_t sampleRate = 0;
};

enum class VoiceState
{
    Idle,
    Playing,
    Paused,
    Complete
};
enum class VoiceSegment
{
    None,
    Leadin,
    Loop
};

struct PlaybackCursor
{
    VoiceState state;
    VoiceSegment segment;
    std::uint32_t frame;
    int timeMS;
};

class VoiceTimeline final
{
  public:
    void Configure(SampleTiming leadin, SampleTiming looping = {});
    void Start(int offsetMS, std::uint64_t now);
    void Advance(std::uint64_t now);
    void Pause(std::uint64_t now);
    void UnPause(std::uint64_t now);
    void Stop(std::uint64_t now);
    // Quantized to Q16, including zero (freeze), with release validation. Changes apply from now onward.
    void SetPitch(float pitch, std::uint64_t now);
    PlaybackCursor GetCursor() const;
    float GetPitch() const;

  private:
    void CheckClock(std::uint64_t now);
    SampleTiming m_leadin;
    SampleTiming m_looping;
    std::uint64_t m_leadinLength = 0;
    std::uint64_t m_loopLength = 0;
    std::uint64_t m_phase = 0; // Q16 microseconds, bounded within the current segment.
    std::uint64_t m_lastTime = 0;
    std::uint32_t m_pitch = 65536;
    VoiceState m_state = VoiceState::Idle;
    VoiceSegment m_segment = VoiceSegment::None;
    bool m_haveClock = false;
};
} // namespace ps2::audio
