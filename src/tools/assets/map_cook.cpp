// ================================================================================================
// File: map_cook.cpp
// Brief: The D3ME writer and the readers of the geometry CRC in a map's .bcm and .aas files.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/map_cook.h"
#include "assets/bytes.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <span>

namespace assets
{

namespace
{

// The layout of src/ps2/game/map_format.h, which validates it.
static constexpr std::uint32_t kMagic = 0x454D3344; // "D3ME"
static constexpr std::uint32_t kVersion = 1;
static constexpr std::size_t kHeaderBytes = 32;
static constexpr std::size_t kMaxFileBytes = 8U * 1024U * 1024U;
static constexpr std::size_t kMaxEntities = 1U << 16;
static constexpr std::size_t kMaxPairs = 1U << 20;

// idStr::StripTrailingWhitespace: every byte up to ' ', high ASCII kept.
std::string StripTrailing(const std::string & text)
{
    std::size_t length = text.size();

    while (length > 0 && static_cast<unsigned char>(text[length - 1]) <= ' ')
    {
        --length;
    }

    return text.substr(0, length);
}

} // namespace

bool CollisionGeometryCrc(const std::vector<std::uint8_t> & bcm, std::uint32_t & crc, std::string & error)
{
    if (bcm.size() < 12)
    {
        error = "collision file shorter than its header";
        return false;
    }

    const std::uint32_t nameLength = ReadLe32(bcm.data() + 4);

    if (nameLength > bcm.size() - 12)
    {
        error = "collision file's map name runs past the file";
        return false;
    }

    crc = ReadBe32(bcm.data() + 8 + nameLength);
    return true;
}

bool AasGeometryCrc(std::string_view text, std::uint32_t & crc, std::string & error)
{
    if (text.substr(0, 7) != "DewmAAS")
    {
        error = "not an AAS file";
        return false;
    }

    std::size_t at = text.find('\n');

    while (at != std::string_view::npos && at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
    {
        ++at;
    }

    std::uint64_t value = 0;
    std::size_t digits = 0;

    while (at != std::string_view::npos && at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) && digits < 10)
    {
        value = value * 10U + static_cast<std::uint64_t>(text[at] - '0');
        ++at;
        ++digits;
    }

    if (digits == 0 || value > 0xFFFFFFFFULL)
    {
        error = "AAS file names no CRC after its version";
        return false;
    }

    crc = static_cast<std::uint32_t>(value);
    return true;
}

bool WriteCookedMap(const std::vector<MapEntity> & entities, std::uint32_t geometryCrc, std::vector<std::uint8_t> & output,
                    std::string & error)
{
    std::size_t pairCount = 0;

    for (const MapEntity & entity : entities)
    {
        pairCount += entity.written.size();
    }

    if (entities.empty() || entities.size() > kMaxEntities || pairCount > kMaxPairs)
    {
        error = "entity or pair count outside the D3ME format's bounds";
        return false;
    }

    // Strings once each, in first-use order.
    std::vector<std::uint8_t> strings;
    std::map<std::string, std::uint32_t> offsets;
    const auto Intern = [&](const std::string & text) -> std::uint32_t
    {
        const auto found = offsets.find(text);

        if (found != offsets.end())
        {
            return found->second;
        }

        const auto offset = static_cast<std::uint32_t>(strings.size());
        strings.insert(strings.end(), text.begin(), text.end());
        strings.push_back(0);
        offsets.emplace(text, offset);
        return offset;
    };

    std::vector<std::uint32_t> pairs;
    pairs.reserve(pairCount * 2U);

    for (const MapEntity & entity : entities)
    {
        for (const auto & [key, value] : entity.written)
        {
            const std::string strippedKey = StripTrailing(key);

            if (strippedKey.empty())
            {
                error = "an entity has an empty key";
                return false;
            }

            pairs.push_back(Intern(strippedKey));
            pairs.push_back(Intern(StripTrailing(value)));
        }
    }

    // The block is padded with NULs to whole words, so the file stays 4-byte sized.
    while (strings.empty() || (strings.size() & 3U) != 0)
    {
        strings.push_back(0);
    }

    const std::size_t bytes = kHeaderBytes + entities.size() * 4U + pairs.size() * 4U + strings.size();

    if (bytes > kMaxFileBytes)
    {
        error = "cooked map larger than the D3ME format allows";
        return false;
    }

    output.assign(bytes, 0);
    std::uint8_t * at = output.data() + kHeaderBytes;

    for (const MapEntity & entity : entities)
    {
        WriteLe32(at, static_cast<std::uint32_t>(entity.written.size()));
        at += 4;
    }

    for (const std::uint32_t value : pairs)
    {
        WriteLe32(at, value);
        at += 4;
    }

    std::copy(strings.begin(), strings.end(), at);

    std::uint8_t * const header = output.data();
    WriteLe32(header + 0, kMagic);
    WriteLe32(header + 4, kVersion);
    WriteLe32(header + 8, geometryCrc);
    WriteLe32(header + 12, static_cast<std::uint32_t>(entities.size()));
    WriteLe32(header + 16, static_cast<std::uint32_t>(pairCount));
    WriteLe32(header + 20, static_cast<std::uint32_t>(strings.size()));
    WriteLe32(header + 24, Fnv1a32(std::span<const std::uint8_t>(output).subspan(kHeaderBytes)));
    WriteLe32(header + 28, 0);
    return true;
}

} // namespace assets
