// ================================================================================================
// File: voice_timeline.cpp
// Brief: Preserve exact frame boundaries and fractional pitch without accumulating unbounded elapsed time.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/audio/voice_timeline.h"
#include "ps2/audio/pcm_wave.h"
#include "ps2/common.h"

namespace
{
static constexpr std::uint64_t kPitchScale = 65536;
static constexpr std::uint64_t kUnitsPerSecond = 1000000 * kPitchScale;

std::uint64_t SampleLength(ps2::audio::SampleTiming sample)
{
    if (sample.sampleRate < 8000 || sample.sampleRate > 48000 || sample.frames == 0 ||
        sample.frames > ps2::audio::kMaxPcmBytes / 2 ||
        static_cast<std::uint64_t>(sample.frames) * 1000 < sample.sampleRate)
    {
        ps2::FatalError("invalid logical voice sample timing");
    }
    // Round only at 1/65536 microsecond resolution, never at the native integer-millisecond duration.
    return (static_cast<std::uint64_t>(sample.frames) * kUnitsPerSecond + sample.sampleRate - 1) / sample.sampleRate;
}
} // namespace

namespace ps2::audio
{
void VoiceTimeline::Configure(SampleTiming leadin, SampleTiming looping)
{
    const std::uint64_t leadinLength = SampleLength(leadin);
    const bool hasLoop = looping.frames != 0 || looping.sampleRate != 0;
    const std::uint64_t loopLength = hasLoop ? SampleLength(looping) : 0;
    *this = {};
    m_leadin = leadin;
    m_looping = looping;
    m_leadinLength = leadinLength;
    m_loopLength = loopLength;
}

void VoiceTimeline::CheckClock(std::uint64_t now)
{
    if (m_haveClock && now < m_lastTime)
    {
        FatalError("logical audio clock moved backwards");
    }
    m_haveClock = true;
    m_lastTime = now;
}

void VoiceTimeline::Start(int offsetMS, std::uint64_t now)
{
    if (m_leadinLength == 0)
    {
        FatalError("logical voice requires configured samples");
    }
    if (offsetMS < 0)
    {
        FatalError("logical voice start offset is negative");
    }
    CheckClock(now);
    m_phase = static_cast<std::uint64_t>(offsetMS) * 1000 * kPitchScale;
    m_state = VoiceState::Playing;
    m_segment = VoiceSegment::Leadin;
    if (m_phase >= m_leadinLength)
    {
        if (m_loopLength != 0)
        {
            m_phase = (m_phase - m_leadinLength) % m_loopLength;
            m_segment = VoiceSegment::Loop;
        }
        else
        {
            m_phase = 0;
            m_state = VoiceState::Complete;
            m_segment = VoiceSegment::None;
        }
    }
}

void VoiceTimeline::Advance(std::uint64_t now)
{
    const std::uint64_t elapsed = m_haveClock && now >= m_lastTime ? now - m_lastTime : 0;
    CheckClock(now);
    if (m_state != VoiceState::Playing || m_pitch == 0)
    {
        return;
    }
    if (m_segment == VoiceSegment::Leadin)
    {
        const std::uint64_t remaining = m_leadinLength - m_phase;
        if (elapsed <= (remaining - 1) / m_pitch)
        {
            m_phase += elapsed * m_pitch;
            return;
        }
        if (m_loopLength == 0)
        {
            m_phase = 0;
            m_state = VoiceState::Complete;
            m_segment = VoiceSegment::None;
            return;
        }
        // Reduce before multiplying: even a UINT64_MAX clock jump must not overflow.
        m_phase = ((elapsed % m_loopLength) * m_pitch % m_loopLength + m_phase % m_loopLength +
                   m_loopLength - m_leadinLength % m_loopLength) %
                  m_loopLength;
        m_segment = VoiceSegment::Loop;
    }
    else
    {
        m_phase = (m_phase + (elapsed % m_loopLength) * m_pitch % m_loopLength) % m_loopLength;
    }
}

void VoiceTimeline::Pause(std::uint64_t now)
{
    Advance(now);
    if (m_state == VoiceState::Playing)
    {
        m_state = VoiceState::Paused;
    }
}

void VoiceTimeline::UnPause(std::uint64_t now)
{
    Advance(now);
    if (m_state == VoiceState::Paused)
    {
        m_state = VoiceState::Playing;
    }
}

void VoiceTimeline::Stop(std::uint64_t now)
{
    Advance(now);
    m_phase = 0;
    m_state = VoiceState::Idle;
    m_segment = VoiceSegment::None;
}

void VoiceTimeline::SetPitch(float pitch, std::uint64_t now)
{
    if (!(pitch >= 0.0f && pitch <= 8.0f))
    {
        FatalError("logical voice pitch must be between 0 and 8");
    }
    Advance(now); // Charge elapsed time to the old pitch before changing the rate.
    m_pitch = static_cast<std::uint32_t>(pitch * static_cast<float>(kPitchScale) + 0.5f);
}

PlaybackCursor VoiceTimeline::GetCursor() const
{
    if (m_segment == VoiceSegment::None)
    {
        return { m_state, m_segment, 0, 0 };
    }
    const SampleTiming sample = m_segment == VoiceSegment::Leadin ? m_leadin : m_looping;
    PS2_Assert(m_phase < (m_segment == VoiceSegment::Leadin ? m_leadinLength : m_loopLength));
    return { m_state, m_segment, static_cast<std::uint32_t>(m_phase * sample.sampleRate / kUnitsPerSecond),
             static_cast<int>(m_phase / (1000 * kPitchScale)) };
}

float VoiceTimeline::GetPitch() const { return static_cast<float>(m_pitch) / static_cast<float>(kPitchScale); }
} // namespace ps2::audio
