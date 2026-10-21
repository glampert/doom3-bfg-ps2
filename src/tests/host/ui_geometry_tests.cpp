// ================================================================================================
// File: ui_geometry_tests.cpp
// Brief: Exercise native UI clipping, subpixels and hostile float inputs independently of the GS.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/geometry.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{

using ps2::ui::FixedRect;
using ps2::ui::MapRect;
using ps2::ui::Rect;
using ps2::ui::Result;
using ps2::ui::Viewport;

static constexpr Viewport kNtsc{ 512, 448 };
static constexpr Viewport kPal{ 512, 512 };
static constexpr Rect kFull{ 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 1.0f, 1.0f };
static constexpr FixedRect kSentinel{ -1, -2, -3, -4, -5, -6, -7, -8 };

static bool Equal(const FixedRect & a, const FixedRect & b)
{
    return (a.x0 == b.x0) && (a.y0 == b.y0) && (a.x1 == b.x1) && (a.y1 == b.y1) &&
           (a.u0 == b.u0) && (a.v0 == b.v0) && (a.u1 == b.u1) && (a.v1 == b.v1);
}

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI geometry failed: %s\n", name);
    }
    return passed;
}

static bool Unchanged(const Rect & rect, Viewport viewport, int width, int height, Result expected)
{
    FixedRect output = kSentinel;
    return (MapRect(rect, viewport, width, height, output) == expected) && Equal(output, kSentinel);
}

static bool Coordinates()
{
    FixedRect output{};
    if (!Check((MapRect(kFull, kNtsc, 64, 32, output) == Result::Draw) &&
               Equal(output, { 0, 0, 8192, 7168, 0, 0, 1024, 512 }),
               "NTSC full viewport") ||
        !Check((MapRect(kFull, kPal, 64, 32, output) == Result::Draw) &&
               Equal(output, { 0, 0, 8192, 8192, 0, 0, 1024, 512 }),
               "PAL full viewport"))
    {
        return false;
    }

    const Rect clipped{ -160.0f, -120.0f, 320.0f, 240.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    if (!Check((MapRect(clipped, kNtsc, 64, 32, output) == Result::Draw) &&
               Equal(output, { 0, 0, 2048, 1792, 512, 256, 1024, 512 }),
               "clipped UV quadrants"))
    {
        return false;
    }

    const Rect lowerRight{ 480.0f, 360.0f, 320.0f, 240.0f, 0.25f, 0.25f, 0.75f, 0.75f };
    if (!Check((MapRect(lowerRight, kNtsc, 64, 32, output) == Result::Draw) &&
               Equal(output, { 6144, 5376, 8192, 7168, 256, 128, 512, 256 }),
               "clipped right/bottom"))
    {
        return false;
    }

    const Rect fractional{ 0.5f, 0.5f, 1.25f, 1.25f, 0.25f, 0.125f, 0.75f, 0.875f };
    if (!Check((MapRect(fractional, kNtsc, 64, 32, output) == Result::Draw) &&
               Equal(output, { 6, 7, 22, 26, 256, 64, 768, 448 }),
               "fractional destination"))
    {
        return false;
    }

    FixedRect neighbor{};
    const Rect first{ 0.0f, 0.0f, 13.25f, 480.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    const Rect second{ 13.25f, 0.0f, 626.75f, 480.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    if (!Check((MapRect(first, kNtsc, 1, 1, output) == Result::Draw) &&
               (MapRect(second, kNtsc, 1, 1, neighbor) == Result::Draw) &&
               (output.x1 == neighbor.x0) && (output.x1 == 170),
               "adjacent fractional seams"))
    {
        return false;
    }

    const Rect repeat{ 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 2.0f, 2.0f };
    const Rect largestUv{ 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 0.0f, 1023.0f, 1023.0f };
    return Check((MapRect(repeat, kNtsc, 64, 32, output) == Result::Draw) &&
                 Equal(output, { 0, 0, 8192, 7168, 0, 0, 2048, 1024 }),
                 "repeat UV span") &&
           Check((MapRect(kFull, kNtsc, 3, 5, output) == Result::Draw) &&
                 (output.u1 == 48) && (output.v1 == 80),
                 "logical NPOT extent") &&
           Check((MapRect(largestUv, kNtsc, 1, 1, output) == Result::Draw) &&
                 (output.u1 == 16368) && (output.v1 == 16368),
                 "largest safe UV field") &&
           Check(Unchanged(repeat, kNtsc, 512, 512, Result::Invalid), "repeat cannot wrap UV field");
}

static bool Rejections()
{
    const Rect clipped[] = {
        { -100.0f, 0.0f, 100.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 640.0f, 0.0f, 100.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, -100.0f, 20.0f, 100.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 480.0f, 20.0f, 100.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 20.0f, -0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.001f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 32768.0f, 32768.0f, 32768.0f, 32768.0f, 0.0f, 0.0f, 1.0f, 1.0f }
    };
    for (const Rect & rect : clipped)
    {
        if (!Check(Unchanged(rect, kNtsc, 64, 32, Result::Clipped), "empty leaves output intact"))
        {
            return false;
        }
    }

    const Rect invalid[] = {
        { 0.0f, 0.0f, -1.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 20.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { -32769.0f, 0.0f, 20.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 32769.0f, 20.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 32769.0f, 20.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 20.0f, 20.0f, -0.01f, 0.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 20.0f, 20.0f, 0.5f, 0.0f, 0.25f, 1.0f },
        { 0.0f, 0.0f, 20.0f, 20.0f, 0.0f, 0.75f, 1.0f, 0.25f },
        { 0.0f, 0.0f, 20.0f, 20.0f, 0.0f, 0.0f, 16.0f, 1.0f }
    };
    for (const Rect & rect : invalid)
    {
        if (!Check(Unchanged(rect, kNtsc, 64, 32, Result::Invalid), "invalid leaves output intact"))
        {
            return false;
        }
    }

    const Viewport badViewports[] = { { 0, 448 }, { 512, -1 }, { 1025, 512 }, { 512, 1025 } };
    for (Viewport viewport : badViewports)
    {
        if (!Check(Unchanged(kFull, viewport, 64, 32, Result::Invalid), "invalid viewport"))
        {
            return false;
        }
    }
    return Check(Unchanged(kFull, kNtsc, 0, 32, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 64, -1, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 513, 32, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 64, 513, Result::Invalid),
                 "invalid texture dimensions");
}

static bool FloatEncodings()
{
    static constexpr float Rect::* kFields[] = {
        &Rect::x, &Rect::y, &Rect::width, &Rect::height, &Rect::s0, &Rect::t0, &Rect::s1, &Rect::t1
    };
    static constexpr std::uint32_t kInvalidBits[] = {
        0x7F800000U, 0xFF800000U, 0x7FC00000U, 0xFFC00000U, 0x7F800001U, 0x00000001U, 0x80000001U
    };
    for (std::uint32_t bits : kInvalidBits)
    {
        for (float Rect::* field : kFields)
        {
            Rect rect = kFull;
            rect.*field = std::bit_cast<float>(bits);
            if (!Check(Unchanged(rect, kNtsc, 64, 32, Result::Invalid), "nonportable float bits"))
            {
                return false;
            }
        }
    }

    for (float Rect::* field : kFields)
    {
        Rect rect = kFull;
        rect.*field = std::numeric_limits<float>::max();
        if (!Check(Unchanged(rect, kNtsc, 64, 32, Result::Invalid), "finite arithmetic overflow input"))
        {
            return false;
        }
    }
    Rect rect = kFull;
    rect.x = -0.0f;
    rect.y = -0.0f;
    rect.s0 = -0.0f;
    rect.t0 = -0.0f;
    FixedRect output{};
    return Check(MapRect(rect, kNtsc, 512, 512, output) == Result::Draw, "signed zero accepted");
}

} // namespace

int main()
{
    return Coordinates() && Rejections() && FloatEncodings() ? 0 : 1;
}
