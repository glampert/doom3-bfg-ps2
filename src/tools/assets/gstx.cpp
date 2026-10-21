// ================================================================================================
// File: gstx.cpp
// Brief: Write and validate GSTX v1, the converted GS texture: paletted index pages, CLUTs and the
// placements that map an engine image's logical texels onto them.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/gstx.h"
#include "assets/bytes.h"
#include "assets/rle.h"

#include <algorithm>
#include <cmath>

namespace assets
{
namespace
{

static constexpr std::uint8_t kEncodingRaw = 0;
static constexpr std::uint8_t kEncodingRle = 1;

static std::size_t Align16(std::size_t value)
{
    return (value + 15U) & ~static_cast<std::size_t>(15U);
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

static bool ValidFormat(std::uint8_t value)
{
    return value == static_cast<std::uint8_t>(IndexFormat::T8) || value == static_cast<std::uint8_t>(IndexFormat::T4);
}

static std::size_t TableBytes(const GstxTexture & texture)
{
    return kGstxHeaderBytes + texture.pages.size() * kGstxPageRecordBytes + texture.cluts.size() * kGstxClutRecordBytes +
           texture.placements.size() * kGstxPlacementRecordBytes;
}

static bool Validate(const GstxTexture & texture, std::string & error)
{
    const Rect logical{ 0, 0, texture.logicalWidth, texture.logicalHeight };

    if (texture.logicalWidth < 1 || texture.logicalHeight < 1 || texture.logicalWidth > kGstxMaxLogicalExtent ||
        texture.logicalHeight > kGstxMaxLogicalExtent)
    {
        error = "GSTX logical dimensions out of range";
        return false;
    }

    if (texture.pages.empty() || texture.pages.size() > kGstxMaxPages || texture.cluts.empty() ||
        texture.cluts.size() > kGstxMaxCluts || texture.placements.empty() || texture.placements.size() > kGstxMaxPlacements)
    {
        error = "GSTX page, CLUT or placement count out of range";
        return false;
    }

    for (const GstxPage & page : texture.pages)
    {
        if (page.width < GsPageWidth(page.format) || page.width > kGstxMaxPageExtent || page.width % GsPageWidth(page.format) != 0 ||
            page.height < GsPageHeight(page.format) || page.height > kGstxMaxPageExtent || page.height % GsPageHeight(page.format) != 0 ||
            page.data.size() != IndexBytes(page.format, page.width, page.height))
        {
            error = "GSTX page dimensions or data size invalid";
            return false;
        }
    }

    for (const GstxClut & clut : texture.cluts)
    {
        if (clut.texels.size() != static_cast<std::size_t>(PaletteSize(clut.format)))
        {
            error = "GSTX CLUT entry count invalid";
            return false;
        }
    }

    for (std::size_t index = 0; index < texture.placements.size(); ++index)
    {
        const GstxPlacement & placement = texture.placements[index];

        if (placement.page < 0 || static_cast<std::size_t>(placement.page) >= texture.pages.size() || placement.clut < 0 ||
            static_cast<std::size_t>(placement.clut) >= texture.cluts.size())
        {
            error = "GSTX placement references a missing page or CLUT";
            return false;
        }

        const GstxPage & page = texture.pages[static_cast<std::size_t>(placement.page)];
        const Rect pageRect{ 0, 0, page.width, page.height };

        if (texture.cluts[static_cast<std::size_t>(placement.clut)].format != page.format)
        {
            error = "GSTX placement CLUT does not match its page format";
            return false;
        }

        if (placement.match.width < 1 || placement.match.height < 1 || !Inside(placement.match, placement.source) ||
            !Inside(placement.source, logical) || placement.dest.width < 1 || placement.dest.height < 1 ||
            placement.dest.width > placement.source.width || placement.dest.height > placement.source.height ||
            !Inside(placement.dest, pageRect))
        {
            error = "GSTX placement rectangles invalid";
            return false;
        }

        for (std::size_t other = 0; other < index; ++other)
        {
            if (Overlap(placement.match, texture.placements[other].match))
            {
                error = "GSTX placement match rectangles overlap";
                return false;
            }
        }
    }

    return true;
}

static void PutRect(std::uint8_t * bytes, const Rect & rect)
{
    WriteLe16(bytes, static_cast<std::uint16_t>(rect.x));
    WriteLe16(bytes + 2, static_cast<std::uint16_t>(rect.y));
    WriteLe16(bytes + 4, static_cast<std::uint16_t>(rect.width));
    WriteLe16(bytes + 6, static_cast<std::uint16_t>(rect.height));
}

static Rect GetRect(const std::uint8_t * bytes)
{
    return { ReadLe16(bytes), ReadLe16(bytes + 2), ReadLe16(bytes + 4), ReadLe16(bytes + 6) };
}

static std::vector<std::uint8_t> ClutBytes(const GstxClut & clut)
{
    std::vector<std::uint8_t> bytes;

    for (const PaletteColor & texel : clut.texels)
    {
        bytes.insert(bytes.end(), { texel.r, texel.g, texel.b, texel.a });
    }

    return bytes;
}

// FNV-1a over the header and tables, with the checksum field (bytes 28..31) read as zero.
static std::uint32_t TableChecksum(std::span<const std::uint8_t> tables)
{
    static constexpr std::uint8_t kZero[4] = {};
    std::uint32_t hash = Fnv1a32(tables.subspan(0, 28));
    hash = Fnv1a32(kZero, hash);
    return Fnv1a32(tables.subspan(32), hash);
}

static bool AllZero(std::span<const std::uint8_t> bytes)
{
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value)
                       {
                           return value == 0;
                       });
}

} // namespace

bool WriteGstx(const GstxTexture & texture, std::vector<std::uint8_t> & output, GstxSizes & sizes, std::string & error)
{
    if (!Validate(texture, error))
    {
        return false;
    }

    const std::size_t tableBytes = TableBytes(texture);
    std::vector<std::uint8_t> file(tableBytes, 0);
    sizes = {};
    sizes.tableBytes = tableBytes;

    const auto Append = [&](std::span<const std::uint8_t> block)
    {
        file.resize(Align16(file.size()), 0);
        const std::size_t offset = file.size();
        file.insert(file.end(), block.begin(), block.end());
        return offset;
    };

    std::uint8_t * record = nullptr;

    for (std::size_t index = 0; index < texture.cluts.size(); ++index)
    {
        const std::vector<std::uint8_t> bytes = ClutBytes(texture.cluts[index]);
        const std::size_t offset = Append(bytes);
        record = file.data() + kGstxHeaderBytes + texture.pages.size() * kGstxPageRecordBytes + index * kGstxClutRecordBytes;
        WriteLe16(record, static_cast<std::uint16_t>(texture.cluts[index].texels.size()));
        WriteLe32(record + 4, static_cast<std::uint32_t>(offset));
        WriteLe32(record + 8, static_cast<std::uint32_t>(bytes.size()));
        WriteLe32(record + 12, Fnv1a32(bytes));
        sizes.decodedBytes += bytes.size();
    }

    for (std::size_t index = 0; index < texture.pages.size(); ++index)
    {
        const GstxPage & page = texture.pages[index];
        const std::vector<std::uint8_t> encoded = RleEncode(page.data);
        const bool rle = encoded.size() < page.data.size();
        const std::size_t offset = rle ? Append(encoded) : Append(page.data);
        record = file.data() + kGstxHeaderBytes + index * kGstxPageRecordBytes;
        WriteLe16(record, static_cast<std::uint16_t>(page.width));
        WriteLe16(record + 2, static_cast<std::uint16_t>(page.height));
        record[4] = static_cast<std::uint8_t>(page.format);
        record[5] = rle ? kEncodingRle : kEncodingRaw;
        WriteLe32(record + 8, static_cast<std::uint32_t>(offset));
        WriteLe32(record + 12, static_cast<std::uint32_t>(rle ? encoded.size() : page.data.size()));
        WriteLe32(record + 16, static_cast<std::uint32_t>(page.data.size()));
        WriteLe32(record + 20, Fnv1a32(page.data));
        sizes.decodedBytes += page.data.size();
        sizes.vramBytes += static_cast<std::size_t>(page.width / GsPageWidth(page.format)) *
                           static_cast<std::size_t>(page.height / GsPageHeight(page.format)) * 8192U;
    }

    for (std::size_t index = 0; index < texture.placements.size(); ++index)
    {
        const GstxPlacement & placement = texture.placements[index];
        record = file.data() + kGstxHeaderBytes + texture.pages.size() * kGstxPageRecordBytes +
                 texture.cluts.size() * kGstxClutRecordBytes + index * kGstxPlacementRecordBytes;
        PutRect(record, placement.match);
        PutRect(record + 8, placement.source);
        PutRect(record + 16, placement.dest);
        WriteLe16(record + 24, static_cast<std::uint16_t>(placement.page));
        WriteLe16(record + 26, static_cast<std::uint16_t>(placement.clut));
    }

    file.resize(Align16(file.size()), 0);
    std::uint8_t * header = file.data();
    WriteLe32(header, kGstxMagic);
    WriteLe16(header + 4, kGstxVersion);
    WriteLe16(header + 6, static_cast<std::uint16_t>(kGstxHeaderBytes));
    WriteLe16(header + 8, static_cast<std::uint16_t>(texture.logicalWidth));
    WriteLe16(header + 10, static_cast<std::uint16_t>(texture.logicalHeight));
    WriteLe16(header + 12, static_cast<std::uint16_t>(texture.pages.size()));
    WriteLe16(header + 14, static_cast<std::uint16_t>(texture.cluts.size()));
    WriteLe16(header + 16, static_cast<std::uint16_t>(texture.placements.size()));
    WriteLe32(header + 20, static_cast<std::uint32_t>(tableBytes));
    WriteLe32(header + 24, static_cast<std::uint32_t>(file.size()));
    WriteLe32(header + 28, TableChecksum(std::span<const std::uint8_t>(file.data(), tableBytes)));
    sizes.fileBytes = file.size();
    output = std::move(file);
    return true;
}

bool ReadGstx(std::span<const std::uint8_t> bytes, GstxTexture & output, std::string & error)
{
    error.clear();

    if (bytes.size() < kGstxHeaderBytes || ReadLe32(bytes.data()) != kGstxMagic || ReadLe16(bytes.data() + 4) != kGstxVersion ||
        ReadLe16(bytes.data() + 6) != kGstxHeaderBytes || ReadLe16(bytes.data() + 18) != 0 || !AllZero(bytes.subspan(32, 16)))
    {
        error = "GSTX header invalid";
        return false;
    }

    GstxTexture texture;
    texture.logicalWidth = ReadLe16(bytes.data() + 8);
    texture.logicalHeight = ReadLe16(bytes.data() + 10);
    const std::size_t pageCount = ReadLe16(bytes.data() + 12);
    const std::size_t clutCount = ReadLe16(bytes.data() + 14);
    const std::size_t placementCount = ReadLe16(bytes.data() + 16);
    const std::size_t tableBytes = ReadLe32(bytes.data() + 20);
    const std::size_t fileBytes = ReadLe32(bytes.data() + 24);

    if (pageCount > kGstxMaxPages || clutCount > kGstxMaxCluts || placementCount > kGstxMaxPlacements ||
        tableBytes != kGstxHeaderBytes + pageCount * kGstxPageRecordBytes + clutCount * kGstxClutRecordBytes +
                          placementCount * kGstxPlacementRecordBytes ||
        fileBytes != bytes.size() || tableBytes > fileBytes)
    {
        error = "GSTX counts or sizes invalid";
        return false;
    }

    if (ReadLe32(bytes.data() + 28) != TableChecksum(bytes.subspan(0, tableBytes)))
    {
        error = "GSTX table checksum mismatch";
        return false;
    }

    std::size_t expected = Align16(tableBytes);
    std::size_t previousEnd = tableBytes;

    // Data blocks follow the tables in order, each at the next 16-byte boundary, padded with zeros.
    const auto Block = [&](std::size_t offset, std::size_t size, std::span<const std::uint8_t> & block)
    {
        if (offset != expected || size > fileBytes - offset || !AllZero(bytes.subspan(previousEnd, offset - previousEnd)))
        {
            error = "GSTX data layout is not canonical";
            return false;
        }

        block = bytes.subspan(offset, size);
        previousEnd = offset + size;
        expected = Align16(previousEnd);
        return true;
    };

    const std::uint8_t * clutRecords = bytes.data() + kGstxHeaderBytes + pageCount * kGstxPageRecordBytes;

    for (std::size_t index = 0; index < clutCount; ++index)
    {
        const std::uint8_t * record = clutRecords + index * kGstxClutRecordBytes;
        const std::size_t entries = ReadLe16(record);
        std::span<const std::uint8_t> block;

        if ((entries != 16 && entries != 256) || ReadLe16(record + 2) != 0 || ReadLe32(record + 8) != entries * 4U ||
            !Block(ReadLe32(record + 4), entries * 4U, block) || Fnv1a32(block) != ReadLe32(record + 12))
        {
            error = error.empty() ? "GSTX CLUT record invalid" : error;
            return false;
        }

        GstxClut clut;
        clut.format = entries == 256 ? IndexFormat::T8 : IndexFormat::T4;

        for (std::size_t texel = 0; texel < entries; ++texel)
        {
            clut.texels.push_back({ block[texel * 4], block[texel * 4 + 1], block[texel * 4 + 2], block[texel * 4 + 3] });
        }

        texture.cluts.push_back(std::move(clut));
    }

    for (std::size_t index = 0; index < pageCount; ++index)
    {
        const std::uint8_t * record = bytes.data() + kGstxHeaderBytes + index * kGstxPageRecordBytes;
        GstxPage page;
        page.width = ReadLe16(record);
        page.height = ReadLe16(record + 2);
        const std::uint8_t format = record[4];
        const std::uint8_t encoding = record[5];
        const std::size_t encodedBytes = ReadLe32(record + 12);
        const std::size_t decodedBytes = ReadLe32(record + 16);
        std::span<const std::uint8_t> block;

        if (!ValidFormat(format) || encoding > kEncodingRle || ReadLe16(record + 6) != 0 ||
            !AllZero(std::span<const std::uint8_t>(record + 24, 8)) || page.width < 1 || page.height < 1 ||
            page.width > kGstxMaxPageExtent || page.height > kGstxMaxPageExtent)
        {
            error = "GSTX page record invalid";
            return false;
        }

        page.format = static_cast<IndexFormat>(format);

        if (decodedBytes != IndexBytes(page.format, page.width, page.height) ||
            (encoding == kEncodingRaw && encodedBytes != decodedBytes) || !Block(ReadLe32(record + 8), encodedBytes, block))
        {
            error = error.empty() ? "GSTX page size invalid" : error;
            return false;
        }

        page.data.resize(decodedBytes);

        if (encoding == kEncodingRaw)
        {
            std::copy(block.begin(), block.end(), page.data.begin());
        }
        else if (!RleDecode(block, page.data))
        {
            error = "GSTX page encoding invalid";
            return false;
        }

        if (Fnv1a32(page.data) != ReadLe32(record + 20))
        {
            error = "GSTX page checksum mismatch";
            return false;
        }

        texture.pages.push_back(std::move(page));
    }

    if (Align16(previousEnd) != fileBytes || !AllZero(bytes.subspan(previousEnd)))
    {
        error = "GSTX file has trailing data";
        return false;
    }

    const std::uint8_t * placementRecords = clutRecords + clutCount * kGstxClutRecordBytes;

    for (std::size_t index = 0; index < placementCount; ++index)
    {
        const std::uint8_t * record = placementRecords + index * kGstxPlacementRecordBytes;

        if (ReadLe32(record + 28) != 0)
        {
            error = "GSTX placement record invalid";
            return false;
        }

        GstxPlacement placement;
        placement.match = GetRect(record);
        placement.source = GetRect(record + 8);
        placement.dest = GetRect(record + 16);
        placement.page = ReadLe16(record + 24);
        placement.clut = ReadLe16(record + 26);
        texture.placements.push_back(placement);
    }

    if (!Validate(texture, error))
    {
        return false;
    }

    output = std::move(texture);
    return true;
}

bool SampleGstx(const GstxTexture & texture, double x, double y, PaletteColor & output)
{
    for (const GstxPlacement & placement : texture.placements)
    {
        const Rect & match = placement.match;

        if (x < match.x || y < match.y || x >= match.x + match.width || y >= match.y + match.height)
        {
            continue;
        }

        const GstxPage & page = texture.pages[static_cast<std::size_t>(placement.page)];
        const GstxClut & clut = texture.cluts[static_cast<std::size_t>(placement.clut)];
        const std::vector<PaletteColor> palette = PaletteFromClut(clut.texels, clut.format);
        // Logical position to page texel space, then GS bilinear with the region clamp.
        const double u = placement.dest.x + (x - placement.source.x) * placement.dest.width / placement.source.width - 0.5;
        const double v = placement.dest.y + (y - placement.source.y) * placement.dest.height / placement.source.height - 0.5;
        const int x0 = static_cast<int>(std::floor(u));
        const int y0 = static_cast<int>(std::floor(v));
        const double fx = u - x0;
        const double fy = v - y0;
        double sums[4] = {};

        for (int corner = 0; corner < 4; ++corner)
        {
            const int tx = std::clamp(x0 + (corner & 1), placement.dest.x, placement.dest.x + placement.dest.width - 1);
            const int ty = std::clamp(y0 + (corner >> 1), placement.dest.y, placement.dest.y + placement.dest.height - 1);
            const std::size_t texel = static_cast<std::size_t>(ty) * static_cast<std::size_t>(page.width) + static_cast<std::size_t>(tx);
            const std::uint8_t byte = page.format == IndexFormat::T8 ? page.data[texel] : page.data[texel / 2];
            const std::uint8_t index = page.format == IndexFormat::T8 ? byte : static_cast<std::uint8_t>((texel & 1U) != 0 ? byte >> 4 : byte & 0x0FU);
            const PaletteColor & color = palette[index];
            const double weight = ((corner & 1) != 0 ? fx : 1.0 - fx) * ((corner >> 1) != 0 ? fy : 1.0 - fy);
            sums[0] += weight * color.r;
            sums[1] += weight * color.g;
            sums[2] += weight * color.b;
            sums[3] += weight * color.a;
        }

        output = { static_cast<std::uint8_t>(std::lround(sums[0])), static_cast<std::uint8_t>(std::lround(sums[1])),
                   static_cast<std::uint8_t>(std::lround(sums[2])), static_cast<std::uint8_t>(std::lround(sums[3])) };
        return true;
    }

    return false;
}

} // namespace assets
