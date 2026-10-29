// ================================================================================================
// File: paletted_image.cpp
// Brief: Draw native UI images from converted GSTX textures: paletted pages and CLUTs stay resident,
// and each triangle samples the placement its UV centroid lands in, clamped to that placement.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/paletted_image.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>

#include <climits>
#include <cstring>

namespace ps2::ui
{
namespace
{

// CLUTs live in one 64-texel-wide PSMCT32 texture as 16x16 tiles, four per row. A tile's first
// block within its 8 KiB GS page, for tiles 0..7 of each page; the remaining blocks follow in the
// [0 1; 2 3] order a CSM1 CLUT read expects.
static constexpr u32 kTileBlocks[8] = { 0, 4, 16, 20, 8, 12, 24, 28 };
static constexpr u32 kClutTextureWidth = 64;
static constexpr u32 kMaxGridCells = 64;
static constexpr u32 kMaxGridItems = 65536;

static PaletteStats s_stats = {};

static bool Failed(const char * reason)
{
    Log(LogLevel::Warning, "[D3BFG] UI paletted image rejected: %s\n", reason);
    return false;
}

static u32 ClutBlock(u32 index)
{
    return (index / 8U) * 32U + kTileBlocks[index % 8U];
}

static bool ReadAt(std::FILE * file, u32 offset, void * output, u32 bytes)
{
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 && std::fread(output, 1, bytes, file) == bytes;
}

} // namespace

PalettedImage::~PalettedImage()
{
    Close();
}

bool PalettedImage::IsOpen() const
{
    return m_open;
}

PageImageStats PalettedImage::GetStats() const
{
    return m_stats;
}

const gstx::Header & PalettedImage::GetHeader() const
{
    return m_header;
}

PS2_COLD_FUNC bool PalettedImage::Open(const char * path, const std::uint8_t * descriptor, std::uint32_t descriptorBytes, std::uint32_t fileBytes)
{
    gstx::Header header = {};

    if (m_open || m_tables != nullptr || path == nullptr || renderer::FrameStarted() || !renderer::tex::IsInitialized() ||
        !gstx::DecodeTables(descriptor, descriptorBytes, header) || header.fileBytes != fileBytes)
    {
        return Failed("invalid open state or catalog descriptor");
    }

    std::FILE * file = std::fopen(path, "rb");

    if (file == nullptr)
    {
        return Failed("cannot open texture");
    }

    const bool seeked = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seeked ? std::ftell(file) : -1;
    m_header = header;
    m_tables = static_cast<std::uint8_t *>(system::heap::TryAlloc(descriptorBytes, TAG_RENDER));
    bool loaded = length >= 0 && length <= INT_MAX && static_cast<u32>(length) == fileBytes && m_tables != nullptr &&
                  ReadAt(file, 0, m_tables, descriptorBytes) && std::memcmp(m_tables, descriptor, descriptorBytes) == 0;

    if (!loaded)
    {
        Failed("texture length or tables differ from the catalog");
    }
    else
    {
        // One scratch holds the largest encoded block and its decoded page side by side.
        u32 scratchBytes = 0;

        for (u32 index = 0; index < header.pageCount; ++index)
        {
            const gstx::Page page = gstx::GetPage(m_tables, index);
            scratchBytes = page.dataBytes + page.decodedBytes > scratchBytes ? page.dataBytes + page.decodedBytes : scratchBytes;
        }

        auto * scratch = static_cast<std::uint8_t *>(system::heap::TryAlloc(scratchBytes, TAG_RENDER, 64));
        loaded = scratch != nullptr && Load(file, scratch, scratchBytes) && BuildGrid();
        system::heap::Free(scratch);
    }

    const bool closed = std::fclose(file) == 0;

    if (!loaded || !closed)
    {
        Close();
        return closed ? false : Failed("texture close failed");
    }

    m_open = true;
    ++s_stats.images;
    s_stats.pages += header.pageCount;
    s_stats.cluts += header.clutCount;
    s_stats.pixelBytes += m_stats.cacheBytes;
    s_stats.metadataBytes += m_stats.metadataBytes;
    return true;
}

bool PalettedImage::ReadCluts(std::FILE * file, std::uint8_t * scratch, std::uint32_t scratchBytes, std::uint8_t * texels,
                              int channel)
{
    const u32 rows = (m_header.clutCount + 3U) / 4U * 16U;
    std::memset(texels, 0, kClutTextureWidth * rows * 4U);
    bool white = true;

    for (u32 index = 0; index < m_header.clutCount; ++index)
    {
        const gstx::Clut clut = gstx::GetClut(m_tables, m_header, index);

        if (clut.dataBytes > scratchBytes || !ReadAt(file, clut.dataOffset, scratch, clut.dataBytes) ||
            gstx::Checksum(scratch, clut.dataBytes) != clut.checksum)
        {
            return false;
        }

        const u32 rowTexels = clut.entries == 256 ? 16U : 8U;

        for (u32 entry = 0; entry < clut.entries; ++entry)
        {
            const u32 x = 16U * (index % 4U) + entry % rowTexels;
            const u32 y = 16U * (index / 4U) + entry / rowTexels;
            std::uint8_t * const texel = texels + (y * kClutTextureWidth + x) * 4U;
            std::memcpy(texel, scratch + entry * 4U, 4);
            white = white && (texel[3] == 0 || (texel[0] == 255 && texel[1] == 255 && texel[2] == 255));

            // A screen pass's source is the premultiplied channel: the GS reads it as alpha.
            if (channel >= 0)
            {
                texel[3] = static_cast<std::uint8_t>((static_cast<u32>(texel[channel]) * texel[3] + 127U) / 255U);
            }
        }
    }

    if (channel < 0)
    {
        m_white = white;
    }

    return true;
}

bool PalettedImage::Load(std::FILE * file, std::uint8_t * scratch, std::uint32_t scratchBytes)
{
    // CLUT texture: every palette as a tile, in file order, then one upload-ready RGBA32 image.
    const u32 rows = (m_header.clutCount + 3U) / 4U * 16U;
    const u32 clutBytes = kClutTextureWidth * rows * 4U;
    auto * texels = static_cast<std::uint8_t *>(system::heap::TryAlloc(clutBytes, TAG_RENDER));

    if (texels == nullptr)
    {
        return Failed("CLUT staging allocation");
    }

    bool valid = ReadCluts(file, scratch, scratchBytes, texels, -1);

    if (valid)
    {
        m_cluts = renderer::tex::Create(static_cast<int>(kClutTextureWidth), static_cast<int>(rows), { renderer::tex::Filter::Nearest }, texels, clutBytes,
                              kClutTextureWidth * 4U);
        valid = m_cluts.id != 0;
    }

    system::heap::Free(texels);

    if (!valid)
    {
        return Failed("CLUT block, checksum or texture");
    }

    m_stats.cacheBytes = clutBytes;

    for (u32 index = 0; index < m_header.pageCount; ++index)
    {
        const gstx::Page page = gstx::GetPage(m_tables, index);
        std::uint8_t * encoded = scratch;
        std::uint8_t * decoded = scratch + page.dataBytes;
        bool unpacked = ReadAt(file, page.dataOffset, encoded, page.dataBytes);

        if (unpacked && page.rle)
        {
            unpacked = gstx::RleDecode(encoded, page.dataBytes, decoded, page.decodedBytes);
        }
        else if (unpacked)
        {
            std::memcpy(decoded, encoded, page.decodedBytes);
        }

        if (!unpacked || gstx::Checksum(decoded, page.decodedBytes) != page.checksum)
        {
            return Failed("page block, encoding or checksum");
        }

        const auto format = page.psm == gstx::kPsmT8 ? renderer::tex::PixelFormat::Index8 : renderer::tex::PixelFormat::Index4;
        m_pages[index] = renderer::tex::CreateIndexed(static_cast<int>(page.width), static_cast<int>(page.height), format,
                                            { renderer::tex::Filter::Linear }, decoded, page.decodedBytes);

        if (m_pages[index].id == 0)
        {
            return Failed("paletted page texture budget");
        }

        m_stats.cacheBytes += page.decodedBytes;
    }

    return true;
}

bool PalettedImage::BuildGrid()
{
    const u32 extent = m_header.logicalWidth > m_header.logicalHeight ? m_header.logicalWidth : m_header.logicalHeight;
    m_cellSize = (extent + kMaxGridCells - 1U) / kMaxGridCells;
    m_cellSize = m_cellSize < 32U ? 32U : m_cellSize;
    m_gridColumns = (m_header.logicalWidth + m_cellSize - 1U) / m_cellSize;
    m_gridRows = (m_header.logicalHeight + m_cellSize - 1U) / m_cellSize;
    const u32 cells = m_gridColumns * m_gridRows;
    u32 items = 0;

    // Two passes: count the cells each match rectangle covers, then fill the lists.
    for (int pass = 0; pass < 2; ++pass)
    {
        std::uint32_t total = 0;

        if (pass == 1)
        {
            m_cellStart = static_cast<std::uint32_t *>(system::heap::TryAlloc((cells + 1U) * sizeof(std::uint32_t), TAG_RENDER));
            m_cellItems = static_cast<u16 *>(system::heap::TryAlloc(items * sizeof(u16), TAG_RENDER));

            if (m_cellStart == nullptr || m_cellItems == nullptr)
            {
                return Failed("placement grid allocation");
            }
        }

        for (u32 cell = 0; cell < cells; ++cell)
        {
            if (pass == 1)
            {
                m_cellStart[cell] = total;
            }

            const u32 cellX = (cell % m_gridColumns) * m_cellSize;
            const u32 cellY = (cell / m_gridColumns) * m_cellSize;

            for (u32 index = 0; index < m_header.placementCount; ++index)
            {
                const gstx::Rect match = gstx::GetPlacement(m_tables, m_header, index).match;

                if (match.x < cellX + m_cellSize && cellX < match.x + match.width && match.y < cellY + m_cellSize &&
                    cellY < match.y + match.height)
                {
                    if (pass == 1)
                    {
                        m_cellItems[total] = static_cast<u16>(index);
                    }

                    ++total;
                }
            }
        }

        if (pass == 1)
        {
            m_cellStart[cells] = total;
        }

        items = total;

        if (items == 0 || items > kMaxGridItems)
        {
            return Failed("placement grid bounds");
        }
    }

    m_stats.metadataBytes =
        m_header.tableBytes + (cells + 1U) * static_cast<u32>(sizeof(std::uint32_t)) + items * static_cast<u32>(sizeof(u16));
    return true;
}

bool PalettedImage::IsWhite() const
{
    return m_open && m_white;
}

bool PalettedImage::SupportsScreen() const
{
    return m_open && (m_white || m_channelCluts[0].id != 0);
}

PS2_COLD_FUNC bool PalettedImage::EnableChannelCluts(const char * path)
{
    if (!m_open || path == nullptr || renderer::FrameStarted())
    {
        return Failed("invalid channel CLUT request");
    }

    if (m_white || m_channelCluts[0].id != 0)
    {
        return true;
    }

    const u32 rows = (m_header.clutCount + 3U) / 4U * 16U;
    const u32 clutBytes = kClutTextureWidth * rows * 4U;
    u32 scratchBytes = 0;

    for (u32 index = 0; index < m_header.clutCount; ++index)
    {
        const gstx::Clut clut = gstx::GetClut(m_tables, m_header, index);
        scratchBytes = clut.dataBytes > scratchBytes ? clut.dataBytes : scratchBytes;
    }

    std::FILE * file = std::fopen(path, "rb");
    auto * const scratch = static_cast<std::uint8_t *>(system::heap::TryAlloc(scratchBytes, TAG_RENDER));
    auto * const texels = static_cast<std::uint8_t *>(system::heap::TryAlloc(clutBytes, TAG_RENDER));
    bool valid = file != nullptr && scratch != nullptr && texels != nullptr;

    for (int channel = 0; valid && channel < 3; ++channel)
    {
        valid = ReadCluts(file, scratch, scratchBytes, texels, channel);

        if (valid)
        {
            m_channelCluts[channel] = renderer::tex::Create(static_cast<int>(kClutTextureWidth), static_cast<int>(rows),
                                                            { renderer::tex::Filter::Nearest }, texels, clutBytes, kClutTextureWidth * 4U);
            valid = m_channelCluts[channel].id != 0;
        }
    }

    system::heap::Free(texels);
    system::heap::Free(scratch);
    const bool closed = file == nullptr || std::fclose(file) == 0;

    if (!valid || !closed)
    {
        for (auto & handle : m_channelCluts)
        {
            renderer::tex::Destroy(handle);
            handle = {};
        }

        return Failed("channel CLUT block, checksum or texture");
    }

    m_channelPixelBytes = 3U * clutBytes;
    s_stats.channelCluts += 3U * m_header.clutCount;
    s_stats.channelPixelBytes += m_channelPixelBytes;
    return true;
}

PS2_COLD_FUNC void PalettedImage::Close()
{
    if (m_open)
    {
        --s_stats.images;
        s_stats.pages -= m_header.pageCount;
        s_stats.cluts -= m_header.clutCount;
        s_stats.pixelBytes -= m_stats.cacheBytes;
        s_stats.metadataBytes -= m_stats.metadataBytes;

        if (m_channelCluts[0].id != 0)
        {
            s_stats.channelCluts -= 3U * m_header.clutCount;
            s_stats.channelPixelBytes -= m_channelPixelBytes;
        }
    }

    for (auto & page : m_pages)
    {
        renderer::tex::Destroy(page);
        page = {};
    }

    for (auto & handle : m_channelCluts)
    {
        renderer::tex::Destroy(handle);
        handle = {};
    }

    m_channelPixelBytes = 0;
    m_white = false;
    renderer::tex::Destroy(m_cluts);
    m_cluts = {};
    system::heap::Free(m_tables);
    system::heap::Free(m_cellStart);
    system::heap::Free(m_cellItems);
    m_tables = nullptr;
    m_cellStart = nullptr;
    m_cellItems = nullptr;
    m_header = {};
    m_stats = {};
    m_open = false;
}

bool PalettedImage::Find(const Vertex (&vertices)[3], gstx::Placement & output) const
{
    const float width = static_cast<float>(m_header.logicalWidth);
    const float height = static_cast<float>(m_header.logicalHeight);
    float x = (vertices[0].s + vertices[1].s + vertices[2].s) * width / 3.0f;
    float y = (vertices[0].t + vertices[1].t + vertices[2].t) * height / 3.0f;

    // Native glyph quads reach half a texel past their glyph, so an empty glyph (a space) on the atlas
    // edge puts a triangle's centroid just outside the image: look that point up at the edge.
    if (!(x >= -0.5f && y >= -0.5f && x <= width + 0.5f && y <= height + 0.5f))
    {
        return false;
    }

    x = x < 0.0f ? 0.0f : (x < width ? x : width - 0.5f);
    y = y < 0.0f ? 0.0f : (y < height ? y : height - 0.5f);
    const u32 cell = static_cast<u32>(y) / m_cellSize * m_gridColumns + static_cast<u32>(x) / m_cellSize;

    for (u32 item = m_cellStart[cell]; item < m_cellStart[cell + 1U]; ++item)
    {
        const gstx::Placement placement = gstx::GetPlacement(m_tables, m_header, m_cellItems[item]);
        const gstx::Rect & match = placement.match;

        if (x >= static_cast<float>(match.x) && y >= static_cast<float>(match.y) && x < static_cast<float>(match.x + match.width) &&
            y < static_cast<float>(match.y + match.height))
        {
            output = placement;
            return true;
        }
    }

    // A native fill may address the packer's one-texel clamped border around its image, and a mask
    // can clip such a quad to a sliver whose centroid lies on that border row or column, outside
    // every match rectangle, where a whole quad's centroid never falls. Region clamp samples the
    // image's edge there anyway, so take the nearest placement within one texel.
    float best = 1.0f;
    bool found = false;

    for (u32 index = 0; index < m_header.placementCount; ++index)
    {
        const gstx::Placement placement = gstx::GetPlacement(m_tables, m_header, index);
        const gstx::Rect & match = placement.match;
        const float left = static_cast<float>(match.x);
        const float top = static_cast<float>(match.y);
        const float right = static_cast<float>(match.x + match.width);
        const float bottom = static_cast<float>(match.y + match.height);
        const float dx = x < left ? left - x : (x >= right ? x - right : 0.0f);
        const float dy = y < top ? top - y : (y >= bottom ? y - bottom : 0.0f);
        const float distance = dx > dy ? dx : dy;

        if (distance <= best)
        {
            best = distance;
            output = placement;
            found = true;
        }
    }

    return found;
}

void PalettedImage::Map(const Vertex (&input)[3], const gstx::Placement & placement, Vertex (&output)[3]) const
{
    const gstx::Page page = gstx::GetPage(m_tables, placement.page);
    const float scaleX = static_cast<float>(placement.dest.width) / static_cast<float>(placement.source.width);
    const float scaleY = static_cast<float>(placement.dest.height) / static_cast<float>(placement.source.height);

    for (int corner = 0; corner < 3; ++corner)
    {
        output[corner] = input[corner];
        float u = static_cast<float>(placement.dest.x) +
                  (input[corner].s * static_cast<float>(m_header.logicalWidth) - static_cast<float>(placement.source.x)) * scaleX;
        float v = static_cast<float>(placement.dest.y) +
                  (input[corner].t * static_cast<float>(m_header.logicalHeight) - static_cast<float>(placement.source.y)) * scaleY;
    
        // Native half-texel glyph expansion may reach past a page edge; the region clamp decides sampling.
        u = u < 0.0f ? 0.0f : (u > static_cast<float>(page.width) ? static_cast<float>(page.width) : u);
        v = v < 0.0f ? 0.0f : (v > static_cast<float>(page.height) ? static_cast<float>(page.height) : v);
        output[corner].s = u / static_cast<float>(page.width);
        output[corner].t = v / static_cast<float>(page.height);
    }
}

bool PalettedImage::ValidateTriangle(const Vertex (&vertices)[3]) const
{
#if PS2_D3BFG_ASSERTS
    gstx::Placement placement = {};

    if (!m_open || !renderer::FrameStarted()) [[unlikely]]
    {
        return Failed("invalid triangle validation state");
    }

    if (!Find(vertices, placement)) [[unlikely]]
    {
        Log(LogLevel::Error, "[D3BFG] UI paletted triangle outside converted placements: uv=%d/%d,%d/%d,%d/%d (1/1000)\n",
            static_cast<int>(vertices[0].s * 1000.0f), static_cast<int>(vertices[0].t * 1000.0f),
            static_cast<int>(vertices[1].s * 1000.0f), static_cast<int>(vertices[1].t * 1000.0f),
            static_cast<int>(vertices[2].s * 1000.0f), static_cast<int>(vertices[2].t * 1000.0f));
        return false;
    }

    Vertex mapped[3];
    Map(vertices, placement, mapped);
    const gstx::Page page = gstx::GetPage(m_tables, placement.page);
    FixedTriangle pieces[kMaxClippedTriangles] = {};
    int count = 0;

    return MapTriangle(mapped, { renderer::gs::Width(), renderer::gs::Height() }, static_cast<int>(page.width), static_cast<int>(page.height), pieces, count) != Result::Invalid;
#else // PS2_D3BFG_ASSERTS
    (void)vertices;
    return true;
#endif // PS2_D3BFG_ASSERTS
}

bool PalettedImage::DrawTriangle(const Vertex (&vertices)[3], const renderer::Shading & shading)
{
    gstx::Placement placement = {};

    if (!m_open || !renderer::FrameStarted() || !Find(vertices, placement))
    {
        return Failed("invalid paletted draw state or placement");
    }

    Vertex mapped[3];
    Map(vertices, placement, mapped);
    const gstx::Page page = gstx::GetPage(m_tables, placement.page);
    FixedTriangle pieces[kMaxClippedTriangles] = {};
    int count = 0;
    const Result result = MapTriangle(mapped, { renderer::gs::Width(), renderer::gs::Height() }, static_cast<int>(page.width), static_cast<int>(page.height),
                                      pieces, count);

    if (result == Result::Invalid)
    {
        return Failed("paletted triangle mapping");
    }

    renderer::PalettedDraw draw;
    draw.page = m_pages[placement.page];
    draw.cluts = m_cluts;
    draw.white = m_white;
    draw.clutBlock = ClutBlock(placement.clut);
    draw.regionMinU = static_cast<u16>(placement.dest.x);
    draw.regionMaxU = static_cast<u16>(placement.dest.x + placement.dest.width - 1U);
    draw.regionMinV = static_cast<u16>(placement.dest.y);
    draw.regionMaxV = static_cast<u16>(placement.dest.y + placement.dest.height - 1U);

    for (int channel = 0; channel < 3; ++channel)
    {
        draw.channelCluts[channel] = m_channelCluts[channel];
    }

    for (int triangle = 0; triangle < count; ++triangle)
    {
        if (!renderer::PalettedTriangleFixed(draw, pieces[triangle], shading))
        {
            return Failed("paletted draw submission");
        }

        ++m_stats.triangles;
        ++s_stats.triangles;
    }

    return true;
}

PaletteStats GetPaletteStats()
{
    return s_stats;
}

} // namespace ps2::ui
