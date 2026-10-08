// ================================================================================================
// File: platform_boot.cpp
// Brief: Verify timer progress, DMA-buffer alignment and scalar EE math at platform startup.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "tests/smoketests/platform_boot.h"

#include <math.h>
#include <stdio.h>
#include <tamtypes.h>
#include <timer.h>

namespace ps2::smoketests
{
namespace
{

// Own complete cache lines: no neighbouring state can share a future DMA destination.
alignas(64) static u8 s_dmaBuffer[128] = {};

bool Check(const char * name, bool passed)
{
    printf("[D3BFG] CHECK platform/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool CheckTimer()
{
    constexpr u32 kReadLimit = 8192;
    const u64 first = GetTimerSystemTime();
    u64 previous = first;
    bool monotonic = true;
    for (u32 readIndex = 0; readIndex < kReadLimit; ++readIndex)
    {
        const u64 current = GetTimerSystemTime();
        if (current < previous)
        {
            monotonic = false;
            break;
        }
        previous = current;
    }
    printf("[D3BFG] TIMER bus_ticks=%llu reads=%u\n", previous - first, kReadLimit);
    return Check("timer-progress", monotonic && previous > first);
}

bool CheckTimerConversion()
{
    constexpr u64 kExpectedBusTicks = 184320000;
    const u64 ticks = TimerUSec2BusClock(1, 250000);
    u32 seconds = 0;
    u32 microseconds = 0;
    TimerBusClock2USec(ticks, &seconds, &microseconds);
    return Check("timer-units", ticks == kExpectedBusTicks && seconds == 1 && microseconds == 250000);
}

bool CheckAlignment()
{
    alignas(64) u8 stackBuffer[128] = {};
    const uiptr staticAddress = reinterpret_cast<uiptr>(s_dmaBuffer);
    const uiptr stackAddress = reinterpret_cast<uiptr>(stackBuffer);
    // These are layout checks; a successful emulator run does not establish cache coherency.
    return Check("dma-buffer-alignment", (staticAddress & 63U) == 0 && (stackAddress & 63U) == 0);
}

bool CheckScalarMath()
{
    // Volatile inputs exercise target instructions instead of compile-time constant folding.
    volatile float x = 3.0f;
    volatile float y = 4.0f;
    const float length = sqrtf(x * x + y * y);
    const float inverseLength = 1.0f / length;
    const float unitX = x * inverseLength;
    const float unitY = y * inverseLength;
    constexpr float kTolerance = 0.00001f;
    return Check("scalar-math", sizeof(float) == 4 && fabsf(length - 5.0f) < kTolerance &&
        fabsf(unitX - 0.6f) < kTolerance && fabsf(unitY - 0.8f) < kTolerance);
}

} // namespace

bool RunPlatformTests()
{
    bool passed = CheckTimer();
    passed = CheckTimerConversion() && passed;
    passed = CheckAlignment() && passed;
    passed = CheckScalarMath() && passed;
    return passed;
}

} // namespace ps2::smoketests
