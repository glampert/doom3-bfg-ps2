// ================================================================================================
// File: ui_page_image.cpp
// Brief: Validate compact page tables, verify streamed pixels and fence fixed-slot texture reuse.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_page_image.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>

#include <climits>
#include <cstring>

namespace ps2::ui
{
namespace
{

static bool Failed(const char * reason)
{
    Log(LogLevel::Warning, "[D3BFG] UI page image rejected: %s\n", reason);
    return false;
}

static bool ValidPath(const char * path)
{
    if (path == nullptr)
    {
        return false;
    }

    for (size_t length = 0; length < 256; ++length)
    {
        if (path[length] == '\0')
        {
            return length != 0;
        }
    }

    return false;
}

} // namespace

PageImage::~PageImage()
{
    Close();
}

bool PageImage::IsOpen() const
{
    return m_file != nullptr && m_scratch != nullptr && m_slotCount != 0;
}

const PageHeader & PageImage::GetHeader() const
{
    return m_header;
}

PageImageStats PageImage::GetStats() const
{
    return m_stats;
}

PS2_COLD_FUNC bool PageImage::Open(const char * path, u32 cacheSlots)
{
    if (m_file != nullptr || !rs::IsInitialized() || !ValidPath(path) ||
        cacheSlots == 0 || cacheSlots > kMaxSlots)
    {
        return Failed("invalid open state or cache slot count");
    }

    m_stats = {};
    m_file = std::fopen(path, "rb");

    if (m_file == nullptr)
    {
        return Failed("cannot open package");
    }

    std::uint8_t header[kPageHeaderBytes] = {};
    const bool seeked = std::fseek(m_file, 0, SEEK_END) == 0;
    const long length = seeked ? std::ftell(m_file) : -1;

    if (length < 0 || length > INT_MAX || std::fseek(m_file, 0, SEEK_SET) != 0 ||
        std::fread(header, 1, sizeof(header), m_file) != sizeof(header) ||
        !DecodePageHeader(header, static_cast<std::uint32_t>(length), m_header))
    {
        Close();
        return Failed("invalid package header or length");
    }

    const size_t metadataBytes = static_cast<size_t>(m_header.pageCount) * sizeof(PageRecord);
    m_records = static_cast<PageRecord *>(heap::TryAlloc(metadataBytes, TAG_RENDER));

    if (m_records == nullptr)
    {
        Close();
        return Failed("page table allocation");
    }

    for (u32 page = 0; page < m_header.pageCount; ++page)
    {
        std::uint8_t raw[kPageRecordBytes] = {};

        if (std::fread(raw, 1, sizeof(raw), m_file) != sizeof(raw) ||
            !DecodePageRecord(raw, m_header, page, m_records[page]))
        {
            Close();
            return Failed("invalid page table");
        }
    }

    m_scratch = heap::TryAlloc(kPageBytes, TAG_RENDER, 64);

    if (m_scratch == nullptr)
    {
        Close();
        return Failed("page input allocation");
    }

    std::memset(m_scratch, 0, kPageBytes);
    m_slotCount = cacheSlots;

    for (u32 slot = 0; slot < m_slotCount; ++slot)
    {
        m_slots[slot].texture = tex::Create(kPageExtent, kPageExtent, { tex::Filter::Linear },
                                            m_scratch, kPageBytes, kPageExtent * 4U);

        if (m_slots[slot].texture.id == 0)
        {
            Close();
            return Failed("page cache texture budget");
        }
    }

    m_stats.cacheBytes = cacheSlots * kPageBytes;
    m_stats.scratchBytes = kPageBytes;
    m_stats.metadataBytes = static_cast<u32>(metadataBytes);
    return true;
}

PS2_COLD_FUNC void PageImage::Close()
{
    for (auto & slot : m_slots)
    {
        if (slot.texture.id != 0)
        {
            tex::Destroy(slot.texture);
        }

        slot = {};
    }

    heap::Free(m_scratch);
    heap::Free(m_records);
    m_scratch = nullptr;
    m_records = nullptr;

    if (m_file != nullptr && std::fclose(m_file) != 0)
    {
        Log(LogLevel::Error, "[D3BFG] UI page package close failed\n");
    }

    m_file = nullptr;
    m_header = {};
    m_slotCount = 0;
    m_use = 0;
    m_stats.cacheBytes = 0;
    m_stats.scratchBytes = 0;
    m_stats.metadataBytes = 0;
}

bool PageImage::Load(u32 page, tex::Handle & texture)
{
    PS2_Assert(page < m_header.pageCount);
    Slot * oldest = nullptr;

    for (u32 index = 0; index < m_slotCount; ++index)
    {
        Slot & slot = m_slots[index];

        if (slot.valid && slot.page == page)
        {
            slot.lastUse = ++m_use;
            ++m_stats.hits;
            texture = slot.texture;
            return true;
        }

        if (oldest == nullptr || (!slot.valid && oldest->valid) ||
            (slot.valid == oldest->valid && slot.lastUse < oldest->lastUse))
        {
            oldest = &slot;
        }
    }

    const PageRecord & record = m_records[page];

    if (std::fseek(m_file, static_cast<long>(record.offset), SEEK_SET) != 0 ||
        std::fread(m_scratch, 1, kPageBytes, m_file) != kPageBytes ||
        PageChecksum(m_scratch, kPageBytes) != record.checksum)
    {
        return Failed("page read or checksum");
    }

    PS2_Assert(oldest != nullptr);

    // Update flushes collected native draws; the subsequent Bind drains queued GS references before
    // replacing this texture's VRAM contents. Failed reads never mutate an existing cache slot.
    if (!tex::Update(oldest->texture, m_scratch, kPageBytes, kPageExtent * 4U))
    {
        return Failed("page texture update");
    }

    m_stats.evictions += oldest->valid ? 1U : 0U;
    oldest->valid = true;
    oldest->page = page;
    oldest->lastUse = ++m_use;
    ++m_stats.loads;
    texture = oldest->texture;
    return true;
}

bool PageImage::DrawTriangle(const Vertex (&vertices)[3], gs::BlendMode blend)
{
    if (!IsOpen() || !rs::FrameStarted() ||
        (blend != gs::BlendMode::Replace && blend != gs::BlendMode::StraightAlpha))
    {
        return Failed("invalid draw state");
    }

    FixedTriangle pieces[kMaxPageTriangles] = {};
    int count = 0;
    const Viewport viewport{ gs::Width(), gs::Height() };

    if (MapPageTriangle(vertices, viewport, static_cast<int>(m_header.width),
                        static_cast<int>(m_header.height), 0, 0, pieces, count) == Result::Invalid)
    {
        return Failed("invalid paged triangle");
    }

    float minS = vertices[0].s;
    float maxS = minS;
    float minT = vertices[0].t;
    float maxT = minT;

    for (const auto & vertex : vertices)
    {
        minS = vertex.s < minS ? vertex.s : minS;
        maxS = vertex.s > maxS ? vertex.s : maxS;
        minT = vertex.t < minT ? vertex.t : minT;
        maxT = vertex.t > maxT ? vertex.t : maxT;
    }

    const auto columnAt = [this](float value)
    {
        const u32 column = static_cast<u32>(value * static_cast<float>(m_header.width) / static_cast<float>(kPageInterior));
        return column < m_header.columns ? column : m_header.columns - 1U;
    };
    const auto rowAt = [this](float value)
    {
        const u32 row = static_cast<u32>(value * static_cast<float>(m_header.height) / static_cast<float>(kPageInterior));
        return row < m_header.rows ? row : m_header.rows - 1U;
    };

    ui::Flush();

    for (u32 row = rowAt(minT); row <= rowAt(maxT); ++row)
    {
        for (u32 column = columnAt(minS); column <= columnAt(maxS); ++column)
        {
            const Result result = MapPageTriangle(vertices, viewport, static_cast<int>(m_header.width),
                                                  static_cast<int>(m_header.height), static_cast<int>(column),
                                                  static_cast<int>(row), pieces, count);

            if (result == Result::Invalid)
            {
                return Failed("paged triangle clipping");
            }

            if (result == Result::Clipped)
            {
                continue;
            }

            tex::Handle texture;

            if (!Load(row * m_header.columns + column, texture))
            {
                return false;
            }

            for (int triangle = 0; triangle < count; ++triangle)
            {
                if (!rs::TexturedTriangleFixed(texture, pieces[triangle], blend))
                {
                    return Failed("page draw submission");
                }

                ++m_stats.triangles;
            }
        }
    }

    return true;
}

} // namespace ps2::ui
