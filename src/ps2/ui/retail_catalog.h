// ================================================================================================
// File: retail_catalog.h
// Brief: Validate a compact English UI resource index before publishing borrowed bounded records.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui::retail
{
static constexpr std::size_t kMaxCatalogBytes = 128U * 1024U;
static constexpr std::uint32_t kMaxImages = 32;
static constexpr std::uint32_t kMaxFonts = 8;
static constexpr std::uint32_t kMaxAliases = 128;
static constexpr std::uint32_t kMaxMaterials = 64;
static constexpr std::uint32_t kMaxGlyphSizes = 32;
static constexpr std::uint32_t kMaxGlyphEntries = 4096;
// A glyph set image has one placement per page, each a band mapping 1:1 onto its page.
static constexpr std::uint32_t kMaxGlyphBands = 16;

struct ImageRecord
{
    const char * name = nullptr;
    const char * path = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fileBytes = 0;
    const std::uint8_t * descriptor = nullptr;
    std::uint32_t descriptorBytes = 0;
};

struct FontRecord
{
    const char * family = nullptr;
    const char * logicalPath = nullptr;
    const char * path = nullptr;
    std::uint32_t bytes = 0;
    std::uint32_t checksum = 0;
    std::uint32_t imageIndex = 0;
    std::uint32_t maxAdvance[3] = {};
    std::uint32_t maxHeight[3] = {};
    std::uint32_t index = 0;
    // The font's pre-rendered glyph set: catalog offset and bytes of its table, zero without one.
    std::uint32_t glyphTable = 0;
    std::uint32_t glyphTableBytes = 0;
};

// A glyph set holds a font's glyphs pre-rendered at exact on-screen sizes, in one catalog image
// whose band placements map 1:1 onto its pages. Each size is a run of entries sorted by codepoint.
struct GlyphSetRecord
{
    std::uint32_t imageIndex = 0;
    std::uint32_t sizeCount = 0;
    std::uint32_t entryCount = 0;
};

struct GlyphSize
{
    // GS pixels per atlas texel, and the outline's stroke weight in atlas texels (zero: none); 16.16.
    std::uint32_t scaleX = 0;
    std::uint32_t scaleY = 0;
    std::uint32_t strokeWeight = 0;
    std::uint32_t firstEntry = 0;
    std::uint32_t entryCount = 0;
};

struct GlyphEntry
{
    std::uint32_t codepoint = 0;
    // The bitmap's texels in the image's logical space, inside one band (zero width: nothing to
    // draw). With an outline it holds two bitmaps of one size side by side, the glyph left and
    // its outline right.
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool outline = false;
    // The ink origin, the glyph's left edge on its baseline, in 1/16 GS pixels from a bitmap's corner.
    std::int16_t originX = 0;
    std::int16_t originY = 0;
};

struct MaterialRecord
{
    const char * name = nullptr;
    std::uint32_t imageIndex = 0;
};

// Init is allocation-free and leaves the old view untouched on failure. The caller owns an
// immutable snapshot for the entire view lifetime; records/strings borrow that snapshot.
class CatalogView final
{
public:
    bool Init(const void * bytes, std::size_t length);
    std::uint32_t ImageCount() const;
    std::uint32_t FontCount() const;
    std::uint32_t MaterialCount() const;
    bool GetImage(std::uint32_t index, ImageRecord & output) const;
    bool GetFont(std::uint32_t index, FontRecord & output) const;
    bool GetMaterial(std::uint32_t index, MaterialRecord & output) const;
    bool FindImage(const char * name, ImageRecord & output) const;
    bool FindFont(const char * requested, FontRecord & output) const;
    // False when the font has no glyph set.
    bool GetGlyphSet(const FontRecord & font, GlyphSetRecord & output) const;
    bool GetGlyphSize(const FontRecord & font, std::uint32_t index, GlyphSize & output) const;
    bool GetGlyphEntry(const FontRecord & font, std::uint32_t index, GlyphEntry & output) const;

private:
    bool ValidateGlyphSet(const FontRecord & font, std::uint32_t images) const;
    const char * String(std::uint32_t offset) const;
    const std::uint8_t * m_bytes = nullptr;
};
} // namespace ps2::ui::retail
