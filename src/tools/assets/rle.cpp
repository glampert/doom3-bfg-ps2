// ================================================================================================
// File: rle.cpp
// Brief: Byte-oriented run-length coding for paletted texture pages: transparent padding and flat
// UI fills collapse to a few bytes on disc, and the EE decodes it with one branch per run.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/rle.h"

#include <cstddef>

namespace assets
{
namespace
{

static constexpr std::size_t kMinRun = 3;
static constexpr std::size_t kMaxRun = 130;
static constexpr std::size_t kMaxLiteral = 128;

static std::size_t RunLength(std::span<const std::uint8_t> input, std::size_t start)
{
    std::size_t length = 1;

    while (start + length < input.size() && length < kMaxRun && input[start + length] == input[start])
    {
        ++length;
    }

    return length;
}

} // namespace

std::vector<std::uint8_t> RleEncode(std::span<const std::uint8_t> input)
{
    std::vector<std::uint8_t> output;
    std::size_t cursor = 0;

    while (cursor < input.size())
    {
        const std::size_t run = RunLength(input, cursor);

        if (run >= kMinRun)
        {
            output.push_back(static_cast<std::uint8_t>(128 + run - kMinRun));
            output.push_back(input[cursor]);
            cursor += run;
            continue;
        }

        // Gather literals up to the next run worth encoding.
        std::size_t end = cursor;

        while (end < input.size() && end - cursor < kMaxLiteral && RunLength(input, end) < kMinRun)
        {
            ++end;
        }

        output.push_back(static_cast<std::uint8_t>(end - cursor - 1));
        output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor), input.begin() + static_cast<std::ptrdiff_t>(end));
        cursor = end;
    }

    return output;
}

bool RleDecode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output)
{
    std::size_t read = 0;
    std::size_t written = 0;

    while (read < input.size())
    {
        const std::uint8_t control = input[read++];

        if (control < 128)
        {
            const std::size_t count = static_cast<std::size_t>(control) + 1;

            if (count > input.size() - read || count > output.size() - written)
            {
                return false;
            }

            for (std::size_t index = 0; index < count; ++index)
            {
                output[written++] = input[read++];
            }
        }
        else
        {
            const std::size_t count = static_cast<std::size_t>(control) - 128 + kMinRun;

            if (read >= input.size() || count > output.size() - written)
            {
                return false;
            }

            const std::uint8_t value = input[read++];

            for (std::size_t index = 0; index < count; ++index)
            {
                output[written++] = value;
            }
        }
    }

    return written == output.size();
}

} // namespace assets
