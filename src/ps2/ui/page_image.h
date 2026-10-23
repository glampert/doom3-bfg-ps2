// ================================================================================================
// File: page_image.h
// Brief: Stream cooked atlas pages through fixed texture slots without retaining the full image.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/render_system.h"
#include "ps2/renderer/texture.h"
#include "ps2/ui/page_format.h"
#include "ps2/ui/page_geometry.h"

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

// A native UI image drawn through cooked pages: streamed UIPG RGBA pages (PageImage) or resident
// GSTX paletted pages (PalettedImage). Triangles carry native atlas-normalized UVs.
class PagedImage
{
public:
    virtual ~PagedImage() = default;
    virtual bool IsOpen() const = 0;
    virtual PageImageStats GetStats() const = 0;
    virtual bool ValidateTriangle(const Vertex (&vertices)[3]) const = 0;
    // Screen blending needs each color channel's coverage as alpha (white or channel CLUTs).
    virtual bool SupportsScreen() const = 0;
    // Submission order is preserved. A required caller must handle false; a late failure does not
    // roll back already submitted pieces. Each piece draws every pass of the shading in turn.
    virtual bool DrawTriangle(const Vertex (&vertices)[3], const renderer::Shading & shading) = 0;
};

class PageImage;

// Initialize after the renderer, close every attached image, then shut down before the texture owner. All
// images share fixed texture slots and one input scratch page. Drawing performs no allocations.
class PageCache final
{
public:
    PageCache() = default;
    ~PageCache();
    PageCache(const PageCache &) = delete;
    PageCache & operator=(const PageCache &) = delete;

    bool Init(u32 slots = 2, renderer::tex::Sampler sampler = { renderer::tex::Filter::Linear });
    void Shutdown();
    bool IsInitialized() const;
    PageCacheStats GetStats() const;

private:
    friend class PageImage;

    struct Slot
    {
        renderer::tex::Handle texture;
        const PageImage * image = nullptr;
        u32 page = 0;
        u64 lastUse = 0;
    };

    bool Attach(PageImage & image);
    void Detach(PageImage & image);
    bool Load(PageImage & image, u32 page, renderer::tex::Handle & texture);

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
class PageImage final : public PagedImage
{
public:
    PageImage() = default;
    ~PageImage() override;
    PageImage(const PageImage &) = delete;
    PageImage & operator=(const PageImage &) = delete;

    bool Open(const char * path, u32 cacheSlots = 2);
    bool Open(const char * path, PageCache & cache, bool outerGutter = false);
    void Close();
    bool IsOpen() const override;
    const PageHeader & GetHeader() const;
    const PageRecord * GetRecords() const;
    PageImageStats GetStats() const override;
    bool ValidateTriangle(const Vertex (&vertices)[3]) const override;
    // RGBA pages carry no channel alphas: screen blending is refused.
    bool SupportsScreen() const override;
    bool DrawTriangle(const Vertex (&vertices)[3], const renderer::Shading & shading) override;

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
