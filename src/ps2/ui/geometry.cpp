// ================================================================================================
// File: geometry.cpp
// Brief: Preserve fractional UI positions and interpolate clipped UVs without GS register wrap.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/geometry.h"

#include <bit>
#include <cstdint>

namespace ps2::ui
{
namespace
{

static constexpr float kVirtualWidth = 640.0f;
static constexpr float kVirtualHeight = 480.0f;
static constexpr float kCoordinateLimit = 32768.0f;
static constexpr float kTexelLimit = 1023.0f;
static constexpr int kViewportLimit = 1024;
static constexpr int kTextureLimit = 512;

static bool PortableFloat(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t exponent = bits & 0x7F800000U;
    // EE arithmetic has neither IEEE Inf/NaN nor denormal support. Inspect representations first.
    return (exponent != 0x7F800000U) &&
           ((exponent != 0U) || ((bits & 0x007FFFFFU) == 0U));
}

static bool ValidCoordinate(float value)
{
    return PortableFloat(value) && (value >= -kCoordinateLimit) && (value <= kCoordinateLimit);
}

static bool ValidUv(float begin, float end, int textureExtent)
{
    const float normalizedLimit = kTexelLimit / static_cast<float>(textureExtent);
    return PortableFloat(begin) && PortableFloat(end) && (begin >= 0.0f) &&
           (end >= begin) && (end <= normalizedLimit);
}

static float Clamp(float value, float lower, float upper)
{
    if (value < lower)
    {
        return lower;
    }
    return (value > upper) ? upper : value;
}

static int RoundFixed(float value)
{
    // All callers have clipped or validated nonnegative values well inside the int range.
    return static_cast<int>((value * 16.0f) + 0.5f);
}

static float ClipUv(float coordinate, float origin, float size, float begin, float end)
{
    const float fraction = Clamp((coordinate - origin) / size, 0.0f, 1.0f);
    return begin + ((end - begin) * fraction);
}

} // namespace

Result MapRect(const Rect & rect, Viewport viewport, int textureWidth, int textureHeight,
               FixedRect & output)
{
    if ((viewport.width < 1) || (viewport.height < 1) ||
        (viewport.width > kViewportLimit) || (viewport.height > kViewportLimit) ||
        (textureWidth < 1) || (textureHeight < 1) ||
        (textureWidth > kTextureLimit) || (textureHeight > kTextureLimit) ||
        !ValidCoordinate(rect.x) || !ValidCoordinate(rect.y) ||
        !ValidCoordinate(rect.width) || !ValidCoordinate(rect.height) ||
        (rect.width < 0.0f) || (rect.height < 0.0f) ||
        !ValidUv(rect.s0, rect.s1, textureWidth) ||
        !ValidUv(rect.t0, rect.t1, textureHeight))
    {
        return Result::Invalid;
    }

    const float left = Clamp(rect.x, 0.0f, kVirtualWidth);
    const float top = Clamp(rect.y, 0.0f, kVirtualHeight);
    const float right = Clamp(rect.x + rect.width, 0.0f, kVirtualWidth);
    const float bottom = Clamp(rect.y + rect.height, 0.0f, kVirtualHeight);
    if ((right <= left) || (bottom <= top))
    {
        return Result::Clipped;
    }

    const float scaleX = static_cast<float>(viewport.width) / kVirtualWidth;
    const float scaleY = static_cast<float>(viewport.height) / kVirtualHeight;
    FixedRect mapped{};
    mapped.x0 = RoundFixed(left * scaleX);
    mapped.y0 = RoundFixed(top * scaleY);
    mapped.x1 = RoundFixed(right * scaleX);
    mapped.y1 = RoundFixed(bottom * scaleY);
    if ((mapped.x1 <= mapped.x0) || (mapped.y1 <= mapped.y0))
    {
        return Result::Clipped;
    }

    const float extentS = static_cast<float>(textureWidth);
    const float extentT = static_cast<float>(textureHeight);
    mapped.u0 = RoundFixed(ClipUv(left, rect.x, rect.width, rect.s0, rect.s1) * extentS);
    mapped.v0 = RoundFixed(ClipUv(top, rect.y, rect.height, rect.t0, rect.t1) * extentT);
    mapped.u1 = RoundFixed(ClipUv(right, rect.x, rect.width, rect.s0, rect.s1) * extentS);
    mapped.v1 = RoundFixed(ClipUv(bottom, rect.y, rect.height, rect.t0, rect.t1) * extentT);
    output = mapped;
    return Result::Draw;
}

} // namespace ps2::ui
