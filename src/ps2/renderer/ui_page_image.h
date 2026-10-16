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

// Open after rs initialization; close before its texture owner shuts down. Each reserved slot owns
// one RGBA texture, plus this image's single input scratch page. No allocations occur while drawing.
class PageImage final
{
public:
    PageImage() = default;
    ~PageImage();
    PageImage(const PageImage &) = delete;
    PageImage & operator=(const PageImage &) = delete;

    bool Open(const char * path, u32 cacheSlots = 2);
    void Close();
    bool IsOpen() const;
    const PageHeader & GetHeader() const;
    PageImageStats GetStats() const;
    // Submission order is preserved. A required caller must handle false; a late I/O failure does
    // not roll back already submitted pieces. Only replace and straight-alpha drawing are admitted.
    bool DrawTriangle(const Vertex (&vertices)[3], gs::BlendMode blend);

private:
    struct Slot
    {
        tex::Handle texture;
        u32 page = 0;
        u64 lastUse = 0;
        bool valid = false;
    };

    bool Load(u32 page, tex::Handle & texture);

    static constexpr u32 kMaxSlots = 8;
    std::FILE * m_file = nullptr;
    PageHeader m_header = {};
    PageRecord * m_records = nullptr;
    void * m_scratch = nullptr;
    Slot m_slots[kMaxSlots] = {};
    u32 m_slotCount = 0;
    u64 m_use = 0;
    PageImageStats m_stats = {};
};

} // namespace ps2::ui
