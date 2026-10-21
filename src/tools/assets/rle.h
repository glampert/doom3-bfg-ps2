// ================================================================================================
// File: rle.h
// Brief: Byte-oriented run-length coding for paletted texture pages: transparent padding and flat
// UI fills collapse to a few bytes on disc, and the EE decodes it with one branch per run.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace assets
{

// A control byte n below 128 copies the next n + 1 literal bytes (1..128). A control byte n of 128
// or more repeats the next byte n - 125 times (3..130); shorter runs stay literal.
std::vector<std::uint8_t> RleEncode(std::span<const std::uint8_t> input);

// True only when the input decodes to exactly output.size() bytes and is consumed completely.
bool RleDecode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);

} // namespace assets
