// ================================================================================================
// File: fnv.h
// Brief: 32-bit FNV-1a, the checksum every cooked format carries and the backend's string hash.
// Header-only and on <cstdint>, because the host format tests compile its users too; the host
// asset tools keep their own copy in src/tools/assets/bytes.h, which must agree with this one.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2
{

static constexpr std::uint32_t kFnv1a32Basis = 2166136261U;
static constexpr std::uint32_t kFnv1a32Prime = 16777619U;

// Mixes one byte, for hashes gathered from several places (names, separators).
inline std::uint32_t Fnv1a32Byte(std::uint32_t hash, std::uint8_t byte)
{
    return (hash ^ byte) * kFnv1a32Prime;
}

// Continues `hash` over `length` bytes; pass a previous result to hash discontiguous ranges.
inline std::uint32_t Fnv1a32(const void * bytes, std::size_t length, std::uint32_t hash = kFnv1a32Basis)
{
    const auto * const data = static_cast<const std::uint8_t *>(bytes);

    for (std::size_t index = 0; index < length; ++index)
    {
        hash = Fnv1a32Byte(hash, data[index]);
    }

    return hash;
}

} // namespace ps2
