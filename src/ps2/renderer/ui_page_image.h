// ================================================================================================
// File: ui_page_image.h
// Brief: Stream cooked atlas pages through fixed texture slots without retaining the full image.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/texture.h"
#include "ps2/renderer/ui_page_format.h"
#include "ps2/renderer/ui_page_geometry.h"

#include <cstdio>

namespace ps2::ui
{

struct PageImageStats
{
    u32 loads = 0;
    u32 hits = 0;
    u32 evictions = 0;
    u32 triangles = 0;
    u32 cacheBytes = 0;
    u32 scratchBytes = 0;
    u32 metadataBytes = 0;
};

struct PageCacheStats
{
    u32 loads = 0;
    u32 hits = 0;
    u32 evictions = 0;
    u32 triangles = 0;
    u32 cacheBytes = 0;
    u32 scratchBytes = 0;
    u32 metadataBytes = 0;
    u32 images = 0;
};

class PageImage;

// Initialize after rs, close every attached image, then shut down before the texture owner. All
// images share fixed texture slots and one input scratch page. Drawing performs no allocations.
class PageCache final
{
public:
    PageCache() = default;
    ~PageCache();
    PageCache(const PageCache &) = delete;
    PageCache & operator=(const PageCache &) = delete;

    bool Init(u32 slots = 2, tex::Sampler sampler = { tex::Filter::Linear });
    void Shutdown();
    bool IsInitialized() const;
    PageCacheStats GetStats() const;

private:
    friend class PageImage;

    struct Slot
    {
        tex::Handle texture;
        const PageImage * image = nullptr;
        u32 page = 0;
        u64 lastUse = 0;
    };

    bool Attach(PageImage & image);
    void Detach(PageImage & image);
    bool Load(PageImage & image, u32 page, tex::Handle & texture);

    static constexpr u32 kMaxSlots = 8;
    static constexpr u32 kMaxImages = 32;
    void * m_scratch = nullptr;
    Slot m_slots[kMaxSlots] = {};
    u32 m_slotCount = 0;
    u64 m_use = 0;
    PageCacheStats m_stats = {};
};

// The default overload owns a private cache for standalone callers. Use an explicit PageCache to
// bound the aggregate pixel/scratch budget independently of the number of open atlas images.
class PageImage final
{
public:
    PageImage() = default;
    ~PageImage();
    PageImage(const PageImage &) = delete;
    PageImage & operator=(const PageImage &) = delete;

    bool Open(const char * path, u32 cacheSlots = 2);
    bool Open(const char * path, PageCache & cache, bool outerGutter = false);
    void Close();
    bool IsOpen() const;
    const PageHeader & GetHeader() const;
    const PageRecord * GetRecords() const;
    PageImageStats GetStats() const;
    bool ValidateTriangle(const Vertex (&vertices)[3]) const;
    // Submission order is preserved. A required caller must handle false; a late I/O failure does
    // not roll back already submitted pieces. Replace, straight-alpha and alpha-weighted add are admitted.
    bool DrawTriangle(const Vertex (&vertices)[3], gs::BlendMode blend);

private:
    friend class PageCache;

    std::FILE * m_file = nullptr;
    PageHeader m_header = {};
    PageRecord * m_records = nullptr;
    PageCache * m_cache = nullptr;
    PageCache m_ownedCache;
    PageImageStats m_stats = {};
    bool m_outerGutter = false;
};

} // namespace ps2::ui
