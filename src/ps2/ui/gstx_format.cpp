// ================================================================================================
// File: gstx_format.cpp
// Brief: Validate converted GSTX texture headers and tables before any pixel read or allocation,
// and decode their RLE pages, so a malformed or tampered file is refused at admission.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/gstx_format.h"

namespace ps2::ui::gstx
{
namespace
{

static std::uint32_t Half(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8);
}

static std::uint32_t Word(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static std::uint32_t Align16(std::uint32_t value)
{
    return (value + 15U) & ~15U;
}

static bool Zero(const std::uint8_t * bytes, std::uint32_t count)
{
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (bytes[index] != 0)
        {
            return false;
        }
    }

    return true;
}

static std::uint32_t Fnv(const std::uint8_t * bytes, std::uint32_t length, std::uint32_t hash)
{
    for (std::uint32_t index = 0; index < length; ++index)
    {
        hash = (hash ^ bytes[index]) * 16777619U;
    }

    return hash;
}

// FNV-1a over the header and tables, with the checksum field (bytes 28..31) read as zero.
static std::uint32_t TableChecksum(const std::uint8_t * bytes, std::uint32_t length)
{
    static constexpr std::uint8_t kZero[4] = {};
    const std::uint32_t head = Fnv(kZero, sizeof(kZero), Fnv(bytes, 28, 2166136261U));
    return Fnv(bytes + 32, length - 32U, head);
}

static Rect GetRect(const std::uint8_t * bytes)
{
    return { Half(bytes), Half(bytes + 2), Half(bytes + 4), Half(bytes + 6) };
}

static bool Inside(const Rect & inner, const Rect & outer)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

static bool Overlap(const Rect & first, const Rect & second)
{
    return first.x < second.x + second.width && second.x < first.x + first.width && first.y < second.y + second.height &&
           second.y < first.y + first.height;
}

static std::uint32_t PageHeight(std::uint8_t psm)
{
    return psm == kPsmT8 ? 64U : 128U;
}

static std::uint32_t PageOffset()
{
    return kHeaderBytes;
}

static std::uint32_t ClutOffset(const Header & header)
{
    return kHeaderBytes + header.pageCount * kPageRecordBytes;
}

static std::uint32_t PlacementOffset(const Header & header)
{
    return ClutOffset(header) + header.clutCount * kClutRecordBytes;
}

} // namespace

Page GetPage(const std::uint8_t * tables, std::uint32_t index)
{
    const std::uint8_t * record = tables + PageOffset() + index * kPageRecordBytes;
    return { Half(record), Half(record + 2), record[4], record[5] != 0, Word(record + 8), Word(record + 12), Word(record + 16),
             Word(record + 20) };
}

Clut GetClut(const std::uint8_t * tables, const Header & header, std::uint32_t index)
{
    const std::uint8_t * record = tables + ClutOffset(header) + index * kClutRecordBytes;
    return { Half(record), Word(record + 4), Word(record + 8), Word(record + 12) };
}

Placement GetPlacement(const std::uint8_t * tables, const Header & header, std::uint32_t index)
{
    const std::uint8_t * record = tables + PlacementOffset(header) + index * kPlacementRecordBytes;
    return { GetRect(record), GetRect(record + 8), GetRect(record + 16), Half(record + 24), Half(record + 26) };
}

bool DecodeTables(const std::uint8_t * bytes, std::uint32_t length, Header & output)
{
    if (bytes == nullptr || length < kHeaderBytes || length > kMaxTableBytes || Word(bytes) != kMagic || Half(bytes + 4) != kVersion ||
        Half(bytes + 6) != kHeaderBytes || Half(bytes + 18) != 0 || !Zero(bytes + 32, 16))
    {
        return false;
    }

    Header header = { Half(bytes + 8), Half(bytes + 10), Half(bytes + 12), Half(bytes + 14), Half(bytes + 16), Word(bytes + 20),
                      Word(bytes + 24) };

    if (header.logicalWidth == 0 || header.logicalHeight == 0 || header.logicalWidth > kMaxLogicalExtent ||
        header.logicalHeight > kMaxLogicalExtent || header.pageCount == 0 || header.pageCount > kMaxPages || header.clutCount == 0 ||
        header.clutCount > kMaxCluts || header.placementCount == 0 || header.placementCount > kMaxPlacements ||
        header.tableBytes != PlacementOffset(header) + header.placementCount * kPlacementRecordBytes || header.tableBytes != length ||
        header.fileBytes > kMaxFileBytes || header.fileBytes < Align16(header.tableBytes) ||
        TableChecksum(bytes, length) != Word(bytes + 28))
    {
        return false;
    }

    // Data blocks follow in table order (CLUTs, then pages), each at the next 16-byte boundary.
    std::uint32_t expected = Align16(header.tableBytes);

    for (std::uint32_t index = 0; index < header.clutCount; ++index)
    {
        const std::uint8_t * record = bytes + ClutOffset(header) + index * kClutRecordBytes;
        const Clut clut = GetClut(bytes, header, index);

        if ((clut.entries != 16 && clut.entries != 256) || Half(record + 2) != 0 || clut.dataBytes != clut.entries * 4U ||
            clut.dataOffset != expected || clut.dataOffset > header.fileBytes || clut.dataBytes > header.fileBytes - clut.dataOffset)
        {
            return false;
        }

        expected = Align16(clut.dataOffset + clut.dataBytes);
    }

    for (std::uint32_t index = 0; index < header.pageCount; ++index)
    {
        const std::uint8_t * record = bytes + PageOffset() + index * kPageRecordBytes;
        const Page page = GetPage(bytes, index);
        const bool known = page.psm == kPsmT8 || page.psm == kPsmT4;

        if (!known || record[5] > 1 || Half(record + 6) != 0 || !Zero(record + 24, 8) || page.width < 128 ||
            page.width > kMaxPageExtent || page.width % 128U != 0 || page.height < PageHeight(page.psm) ||
            page.height > kMaxPageExtent || page.height % PageHeight(page.psm) != 0)
        {
            return false;
        }

        const std::uint32_t decoded = page.psm == kPsmT8 ? page.width * page.height : page.width * page.height / 2U;

        // An RLE page can grow at most by one control byte per 128 literal bytes.
        if (page.decodedBytes != decoded || page.dataBytes == 0 || (!page.rle && page.dataBytes != decoded) ||
            (page.rle && page.dataBytes > decoded + decoded / 128U + 1U) || page.dataOffset != expected ||
            page.dataOffset > header.fileBytes || page.dataBytes > header.fileBytes - page.dataOffset)
        {
            return false;
        }

        expected = Align16(page.dataOffset + page.dataBytes);
    }

    if (expected != header.fileBytes)
    {
        return false;
    }

    const Rect logical = { 0, 0, header.logicalWidth, header.logicalHeight };

    for (std::uint32_t index = 0; index < header.placementCount; ++index)
    {
        const std::uint8_t * record = bytes + PlacementOffset(header) + index * kPlacementRecordBytes;
        const Placement placement = GetPlacement(bytes, header, index);

        if (Word(record + 28) != 0 || placement.page >= header.pageCount || placement.clut >= header.clutCount)
        {
            return false;
        }

        const Page page = GetPage(bytes, placement.page);
        const Clut clut = GetClut(bytes, header, placement.clut);
        const Rect pageRect = { 0, 0, page.width, page.height };

        if ((page.psm == kPsmT8) != (clut.entries == 256) || placement.match.width == 0 || placement.match.height == 0 ||
            !Inside(placement.match, placement.source) || !Inside(placement.source, logical) || placement.dest.width == 0 ||
            placement.dest.height == 0 || placement.dest.width > placement.source.width ||
            placement.dest.height > placement.source.height || !Inside(placement.dest, pageRect))
        {
            return false;
        }

        for (std::uint32_t other = 0; other < index; ++other)
        {
            if (Overlap(placement.match, GetPlacement(bytes, header, other).match))
            {
                return false;
            }
        }
    }

    output = header;
    return true;
}

bool RleDecode(const std::uint8_t * input, std::size_t inputBytes, std::uint8_t * output, std::size_t outputBytes)
{
    std::size_t read = 0;
    std::size_t written = 0;

    while (read < inputBytes)
    {
        const std::uint8_t control = input[read++];

        if (control < 128)
        {
            const std::size_t count = static_cast<std::size_t>(control) + 1U;

            if (count > inputBytes - read || count > outputBytes - written)
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
            const std::size_t count = static_cast<std::size_t>(control) - 125U;

            if (read >= inputBytes || count > outputBytes - written)
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

    return written == outputBytes;
}

} // namespace ps2::ui::gstx
