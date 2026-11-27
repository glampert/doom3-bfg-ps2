// ================================================================================================
// File: map_format.h
// Brief: Validate a cooked D3ME map file: a map's entities and their key/value pairs, with the
// geometry CRC its collision and AAS files were built against, so the PS2 loads a map's entities
// into idMapFile without its lexer or its brushes (the text parse cost seconds and a 12 MB peak, and
// the EE's number conversion gave another CRC).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::game::map
{

// The layout is written by src/tools/assets/map_cook.cpp.
static constexpr std::uint32_t kMagic = 0x454D3344; // "D3ME"
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kHeaderBytes = 32;
static constexpr std::uint32_t kMaxFileBytes = 8U * 1024U * 1024U;
static constexpr std::uint32_t kMaxEntities = 1U << 16;
static constexpr std::uint32_t kMaxPairs = 1U << 20;

// The header, little-endian: the checksum covers every byte after it.
struct Header
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t geometryCrc;
    std::uint32_t entityCount;
    std::uint32_t pairCount;
    std::uint32_t stringBytes;
    std::uint32_t checksum;
    std::uint32_t reserved;
};

static_assert(sizeof(Header) == kHeaderBytes);

// A key/value pair: offsets of two NUL-terminated strings in the string block.
struct Pair
{
    std::uint32_t key;
    std::uint32_t value;
};

// A validated file, viewed in place: entity i has pairCounts[i] pairs, following entity i - 1's.
struct View
{
    const Header * header = nullptr;
    const std::uint32_t * pairCounts = nullptr;
    const Pair * pairs = nullptr;
    const char * strings = nullptr;

    const char * String(std::uint32_t offset) const
    {
        return strings + offset;
    }
};

// Checks every bound, count and string, and the checksum; 'error' names the first failure.
// 'bytes' must stay alive and unchanged while the view is used.
bool Validate(const void * bytes, std::size_t length, View & view, const char *& error);

} // namespace ps2::game::map
