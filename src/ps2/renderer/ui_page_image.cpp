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

PageCache::~PageCache()
{
    Shutdown();
}

bool PageCache::IsInitialized() const
{
    return m_scratch != nullptr && m_slotCount != 0;
}

PageCacheStats PageCache::GetStats() const
{
    return m_stats;
}

PS2_COLD_FUNC bool PageCache::Init(u32 slots, tex::Sampler sampler)
{
    if (m_scratch != nullptr || !rs::IsInitialized() || slots == 0 || slots > kMaxSlots ||
        (sampler.filter != tex::Filter::Nearest && sampler.filter != tex::Filter::Linear) ||
        sampler.wrapS != tex::Wrap::Clamp || sampler.wrapT != tex::Wrap::Clamp)
    {
        return Failed("invalid cache initialization state, slot count or sampler");
    }

    m_stats = {};
    m_scratch = heap::TryAlloc(kPageBytes, TAG_RENDER, 64);

    if (m_scratch == nullptr)
    {
        return Failed("page input allocation");
    }

    std::memset(m_scratch, 0, kPageBytes);
    m_slotCount = slots;

    for (u32 slot = 0; slot < m_slotCount; ++slot)
    {
        m_slots[slot].texture = tex::Create(kPageExtent, kPageExtent, sampler,
                                            m_scratch, kPageBytes, kPageExtent * 4U);

        if (m_slots[slot].texture.id == 0)
        {
            Shutdown();
            return Failed("page cache texture budget");
        }
    }

    m_stats.cacheBytes = slots * kPageBytes;
    m_stats.scratchBytes = kPageBytes;
    return true;
}

PS2_COLD_FUNC void PageCache::Shutdown()
{
    if (m_stats.images != 0)
    {
        FatalError("UI page cache shutdown with %u attached images", m_stats.images);
    }

    for (auto & slot : m_slots)
    {
        if (slot.texture.id != 0)
        {
            tex::Destroy(slot.texture);
        }

        slot = {};
    }

    heap::Free(m_scratch);
    m_scratch = nullptr;
    m_slotCount = 0;
    m_use = 0;
    m_stats.cacheBytes = 0;
    m_stats.scratchBytes = 0;
    m_stats.metadataBytes = 0;
}

PS2_COLD_FUNC bool PageCache::Attach(PageImage & image)
{
    if (!IsInitialized() || m_stats.images == kMaxImages)
    {
        return Failed("unavailable cache or attached image limit");
    }

    ++m_stats.images;
    m_stats.metadataBytes += image.m_stats.metadataBytes;
    return true;
}

PS2_COLD_FUNC void PageCache::Detach(PageImage & image)
{
    if (!IsInitialized() || m_stats.images == 0 ||
        m_stats.metadataBytes < image.m_stats.metadataBytes)
    {
        FatalError("UI page cache attachment mismatch");
    }

    ui::Flush();
    bool referenced = false;

    for (const auto & slot : m_slots)
    {
        referenced = referenced || slot.image == &image;
    }

    if (referenced)
    {
        // Retire the image's queued draws before its identity can be reused. The shared textures
        // remain allocated; their next Update/Bind also fences before replacing the pixel contents.
        cmdbuf::Kick();
        cmdbuf::WaitIdle();
    }

    for (auto & slot : m_slots)
    {
        if (slot.image == &image)
        {
            slot.image = nullptr;
            slot.page = 0;
            slot.lastUse = 0;
        }
    }

    --m_stats.images;
    m_stats.metadataBytes -= image.m_stats.metadataBytes;
}

PageImage::~PageImage()
{
    Close();
}

bool PageImage::IsOpen() const
{
    return m_file != nullptr && m_records != nullptr && m_cache != nullptr && m_cache->IsInitialized();
}

const PageHeader & PageImage::GetHeader() const
{
    return m_header;
}

const PageRecord * PageImage::GetRecords() const
{
    return m_records;
}

PageImageStats PageImage::GetStats() const
{
    PageImageStats stats = m_stats;

    if (m_cache == &m_ownedCache)
    {
        const auto cache = m_ownedCache.GetStats();
        stats.cacheBytes = cache.cacheBytes;
        stats.scratchBytes = cache.scratchBytes;
    }

    return stats;
}

PS2_COLD_FUNC bool PageImage::Open(const char * path, u32 cacheSlots)
{
    if (m_file != nullptr || m_cache != nullptr || !ValidPath(path))
    {
        return Failed("invalid image open state or path");
    }

    if (!m_ownedCache.Init(cacheSlots))
    {
        return false;
    }

    return Open(path, m_ownedCache);
}

PS2_COLD_FUNC bool PageImage::Open(const char * path, PageCache & cache, bool outerGutter)
{
    if (m_file != nullptr || m_cache != nullptr || !cache.IsInitialized() || !ValidPath(path))
    {
        return Failed("invalid image open state, cache or path");
    }

    m_stats = {};
    m_outerGutter = outerGutter;
    m_file = std::fopen(path, "rb");

    if (m_file == nullptr)
    {
        Close();
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

    m_stats.metadataBytes = static_cast<u32>(metadataBytes);

    if (!cache.Attach(*this))
    {
        Close();
        return false;
    }

    m_cache = &cache;
    return true;
}

PS2_COLD_FUNC void PageImage::Close()
{
    if (m_cache != nullptr)
    {
        m_cache->Detach(*this);
        m_cache = nullptr;
    }

    m_ownedCache.Shutdown();
    heap::Free(m_records);
    m_records = nullptr;

    if (m_file != nullptr && std::fclose(m_file) != 0)
    {
        Log(LogLevel::Error, "[D3BFG] UI page package close failed\n");
    }

    m_file = nullptr;
    m_header = {};
    m_outerGutter = false;
    m_stats.cacheBytes = 0;
    m_stats.scratchBytes = 0;
    m_stats.metadataBytes = 0;
}

bool PageCache::Load(PageImage & image, u32 page, tex::Handle & texture)
{
    if (!image.IsOpen() || image.m_cache != this || page >= image.m_header.pageCount)
    {
        return Failed("invalid cache load state or page");
    }

    Slot * oldest = nullptr;

    for (u32 index = 0; index < m_slotCount; ++index)
    {
        Slot & slot = m_slots[index];

        if (slot.image == &image && slot.page == page)
        {
            slot.lastUse = ++m_use;
            ++m_stats.hits;
            ++image.m_stats.hits;
            texture = slot.texture;
            return true;
        }

        const bool valid = slot.image != nullptr;
        const bool oldestValid = oldest != nullptr && oldest->image != nullptr;

        if (oldest == nullptr || (!valid && oldestValid) ||
            (valid == oldestValid && slot.lastUse < oldest->lastUse))
        {
            oldest = &slot;
        }
    }

    const PageRecord & record = image.m_records[page];

    if (std::fseek(image.m_file, static_cast<long>(record.offset), SEEK_SET) != 0 ||
        std::fread(m_scratch, 1, kPageBytes, image.m_file) != kPageBytes ||
        PageChecksum(m_scratch, kPageBytes) != record.checksum)
    {
        return Failed("page read or checksum");
    }

    if (oldest == nullptr)
    {
        return Failed("cache has no texture slot");
    }

    // Update flushes collected native draws; the subsequent Bind drains queued GS references before
    // replacing this texture's VRAM contents. Failed reads never mutate an existing cache slot.
    if (!tex::Update(oldest->texture, m_scratch, kPageBytes, kPageExtent * 4U))
    {
        return Failed("page texture update");
    }

    const u32 eviction = oldest->image != nullptr ? 1U : 0U;
    m_stats.evictions += eviction;
    image.m_stats.evictions += eviction;
    oldest->image = &image;
    oldest->page = page;
    oldest->lastUse = ++m_use;
    ++m_stats.loads;
    ++image.m_stats.loads;
    texture = oldest->texture;
    return true;
}

bool PageImage::ValidateTriangle(const Vertex (&vertices)[3]) const
{
    if (!IsOpen() || !rs::FrameStarted())
    {
        return Failed("invalid triangle validation state");
    }

    FixedTriangle pieces[kMaxPageTriangles] = {};
    int count = 0;
    const Viewport viewport{ gs::Width(), gs::Height() };
    return MapPageTriangle(vertices, viewport, static_cast<int>(m_header.width),
                           static_cast<int>(m_header.height), 0, 0, pieces, count, m_outerGutter) != Result::Invalid;
}

bool PageImage::DrawTriangle(const Vertex (&vertices)[3], gs::BlendMode blend)
{
    if (!IsOpen() || !rs::FrameStarted() ||
        (blend != gs::BlendMode::Replace && blend != gs::BlendMode::StraightAlpha && blend != gs::BlendMode::AdditiveAlpha))
    {
        return Failed("invalid draw state");
    }

    FixedTriangle pieces[kMaxPageTriangles] = {};
    int count = 0;
    const Viewport viewport{ gs::Width(), gs::Height() };

    if (MapPageTriangle(vertices, viewport, static_cast<int>(m_header.width),
                        static_cast<int>(m_header.height), 0, 0, pieces, count, m_outerGutter) == Result::Invalid)
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

    const auto columnAt = [this](float value) -> u32
    {
        if (value <= 0.0f)
        {
            return 0U;
        }

        const u32 column = static_cast<u32>(value * static_cast<float>(m_header.width) / static_cast<float>(kPageInterior));
        return static_cast<u32>(column < m_header.columns ? column : m_header.columns - 1U);
    };
    const auto rowAt = [this](float value) -> u32
    {
        if (value <= 0.0f)
        {
            return 0U;
        }

        const u32 row = static_cast<u32>(value * static_cast<float>(m_header.height) / static_cast<float>(kPageInterior));
        return static_cast<u32>(row < m_header.rows ? row : m_header.rows - 1U);
    };

    ui::Flush();

    for (u32 row = rowAt(minT); row <= rowAt(maxT); ++row)
    {
        for (u32 column = columnAt(minS); column <= columnAt(maxS); ++column)
        {
            const Result result = MapPageTriangle(vertices, viewport, static_cast<int>(m_header.width),
                                                  static_cast<int>(m_header.height), static_cast<int>(column),
                                                  static_cast<int>(row), pieces, count, m_outerGutter);

            if (result == Result::Invalid)
            {
                return Failed("paged triangle clipping");
            }

            if (result == Result::Clipped)
            {
                continue;
            }

            tex::Handle texture;

            if (!m_cache->Load(*this, row * m_header.columns + column, texture))
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
                ++m_cache->m_stats.triangles;
            }
        }
    }

    return true;
}

} // namespace ps2::ui
