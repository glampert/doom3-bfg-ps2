// ================================================================================================
// File: map_format.cpp
// Brief: The D3ME validator: bounds, counts, NUL-terminated strings inside the block and the
// checksum, checked at load in every build before idMapFile takes a pair from the file.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/map_format.h"
#include "ps2/fnv.h"
#include <cstring>

namespace ps2::game::map
{

bool Validate(const void * bytes, std::size_t length, View & view, const char *& error)
{
    view = View{};

    if (bytes == nullptr || length < kHeaderBytes || length > kMaxFileBytes) [[unlikely]]
    {
        error = "file size outside the format's bounds";
        return false;
    }

    // Records are 4-byte aligned in the file; the buffer must be too.
    if ((reinterpret_cast<std::uintptr_t>(bytes) & 3U) != 0) [[unlikely]]
    {
        error = "file buffer is not 4-byte aligned";
        return false;
    }

    const auto * const data = static_cast<const std::uint8_t *>(bytes);
    const auto * const header = static_cast<const Header *>(bytes);

    if (header->magic != kMagic || header->version != kVersion || header->reserved != 0) [[unlikely]]
    {
        error = "not a D3ME version 1 file";
        return false;
    }

    if (header->entityCount == 0 || header->entityCount > kMaxEntities || header->pairCount > kMaxPairs) [[unlikely]]
    {
        error = "entity or pair count outside the format's bounds";
        return false;
    }

    const std::size_t expected = static_cast<std::size_t>(kHeaderBytes) + static_cast<std::size_t>(header->entityCount) * 4U +
                                 static_cast<std::size_t>(header->pairCount) * sizeof(Pair) + header->stringBytes;
    if (header->stringBytes == 0 || header->stringBytes > kMaxFileBytes || expected != length) [[unlikely]]
    {
        error = "section sizes do not add up to the file";
        return false;
    }

    if (Fnv1a32(data + kHeaderBytes, length - kHeaderBytes) != header->checksum) [[unlikely]]
    {
        error = "checksum mismatch";
        return false;
    }

    const auto * const pairCounts = static_cast<const std::uint32_t *>(static_cast<const void *>(data + kHeaderBytes));
    const auto * const pairs = static_cast<const Pair *>(static_cast<const void *>(pairCounts + header->entityCount));
    const auto * const strings = static_cast<const char *>(static_cast<const void *>(pairs + header->pairCount));

    std::uint32_t total = 0;
    for (std::uint32_t entity = 0; entity < header->entityCount; ++entity)
    {
        if (pairCounts[entity] > header->pairCount - total) [[unlikely]]
        {
            error = "entity pair counts exceed the pairs";
            return false;
        }

        total += pairCounts[entity];
    }

    if (total != header->pairCount) [[unlikely]]
    {
        error = "entity pair counts do not cover the pairs";
        return false;
    }

    // Every string ends inside the block, so a block ending in NUL bounds them all.
    if (strings[header->stringBytes - 1] != '\0') [[unlikely]]
    {
        error = "string block does not end in NUL";
        return false;
    }

    for (std::uint32_t index = 0; index < header->pairCount; ++index)
    {
        if (pairs[index].key >= header->stringBytes || pairs[index].value >= header->stringBytes) [[unlikely]]
        {
            error = "pair string outside the string block";
            return false;
        }

        // An empty key never comes from a .map: the parser requires a quoted key.
        if (strings[pairs[index].key] == '\0') [[unlikely]]
        {
            error = "empty key";
            return false;
        }
    }

    view.header = header;
    view.pairCounts = pairCounts;
    view.pairs = pairs;
    view.strings = strings;
    return true;
}

} // namespace ps2::game::map
