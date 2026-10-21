// ================================================================================================
// File: bytes.h
// Brief: Explicit-endian integer access and FNV-1a checksums for the host asset tools, which must
// produce identical files on every host regardless of its native byte order.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace assets
{

inline std::uint16_t ReadLe16(const std::uint8_t * bytes)
{
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

inline std::uint32_t ReadLe32(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline std::uint32_t ReadBe32(const std::uint8_t * bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

inline void WriteLe16(std::uint8_t * bytes, std::uint16_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

inline void WriteLe32(std::uint8_t * bytes, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index)
    {
        bytes[index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFU);
    }
}

// Standard FNV-1a, the checksum the target's UI loaders already use for cooked payloads.
inline std::uint32_t Fnv1a32(std::span<const std::uint8_t> bytes, std::uint32_t seed = 2166136261U)
{
    std::uint32_t hash = seed;

    for (const std::uint8_t byte : bytes)
    {
        hash = (hash ^ byte) * 16777619U;
    }

    return hash;
}

} // namespace assets
