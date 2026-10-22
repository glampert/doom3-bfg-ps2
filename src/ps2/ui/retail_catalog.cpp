// ================================================================================================
// File: retail_catalog.cpp
// Brief: Reject malformed catalog offsets, identities and GSTX texture descriptors before native allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_catalog.h"
#include "ps2/ui/font_format.h"
#include "ps2/ui/gstx_format.h"

#include <cstring>

namespace ps2::ui::retail
{
namespace
{
static constexpr std::uint32_t kVersion = 3;
static constexpr std::uint32_t kHeaderBytes = 64;
static constexpr std::uint32_t kImageBytes = 32;
static constexpr std::uint32_t kFontBytes = 56;
// Glyph set table: u32 image index, size count, entry count, 0; then the sizes and the entries.
static constexpr std::uint32_t kGlyphSetBytes = 16;
static constexpr std::uint32_t kGlyphSizeBytes = 20;
// u32 codepoint, u16 placement, u16 flags (bit 0: outline), s16 origin x/y.
static constexpr std::uint32_t kGlyphEntryBytes = 12;
static constexpr std::uint16_t kGlyphOutlineFlag = 1;
// 16.16 bounds: a glyph drawn at most 16 GS pixels per atlas texel, outlines at most 16 texels.
static constexpr std::uint32_t kMaxGlyphScale = 16U << 16;
static constexpr std::uint32_t kMaxStrokeWeight = 16U << 16;
static constexpr int kMaxGlyphOrigin = 1024 * 16;
static constexpr std::uint32_t kMaxCodepoint = 0x10FFFF;

std::uint32_t Word(const std::uint8_t * bytes, std::uint32_t offset)
{
    const auto * p = bytes + offset;
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t Half(const std::uint8_t * bytes, std::uint32_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::int16_t SignedHalf(const std::uint8_t * bytes, std::uint32_t offset)
{
    return static_cast<std::int16_t>(Half(bytes, offset));
}

bool Path(const char * value)
{
    if (value == nullptr || *value == '\0' || *value == '/' || std::strchr(value, ':') != nullptr ||
        std::strchr(value, '\\') != nullptr)
    {
        return false;
    }

    const char * component = value;

    for (const char * cursor = value;; ++cursor)
    {
        if (*cursor != '\0' && (*cursor < 'a' || *cursor > 'z') && (*cursor < 'A' || *cursor > 'Z') &&
            (*cursor < '0' || *cursor > '9') && *cursor != '/' && *cursor != '_' && *cursor != '-' && *cursor != '.' && *cursor != '#')
        {
            return false;
        }

        if (*cursor == '/' || *cursor == '\0')
        {
            const auto length = cursor - component;

            if (length == 0 || (length == 1 && *component == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.'))
            {
                return false;
            }

            if (*cursor == '\0')
            {
                return true;
            }

            component = cursor + 1;
        }
    }
}

// "assets/" + 64 lowercase hex digits (the content's SHA-256) + suffix.
bool AssetPath(const char * value, const char * suffix)
{
    const std::size_t suffixLength = std::strlen(suffix);

    if (value == nullptr || std::strlen(value) != 71 + suffixLength || std::strncmp(value, "assets/", 7) != 0 ||
        std::strcmp(value + 71, suffix) != 0)
    {
        return false;
    }

    for (int i = 7; i < 71; ++i)
    {
        if ((value[i] < '0' || value[i] > '9') && (value[i] < 'a' || value[i] > 'f'))
        {
            return false;
        }
    }

    return true;
}

bool Family(const char * value)
{
    if (value == nullptr || *value == '\0' || std::strlen(value) > 63)
    {
        return false;
    }

    for (const char * cursor = value; *cursor != '\0'; ++cursor)
    {
        if ((*cursor < 'a' || *cursor > 'z') && (*cursor < '0' || *cursor > '9') && *cursor != '_')
        {
            return false;
        }
    }

    return true;
}

bool FontPath(const char * path, const char * family)
{
    const auto length = std::strlen(family);
    return std::strlen(path) == length + 16 && std::strncmp(path, "newfonts/", 9) == 0 &&
           std::memcmp(path + 9, family, length) == 0 && std::strcmp(path + 9 + length, "/48.dat") == 0;
}

bool Alias(const char * value)
{
    if (value == nullptr || std::strlen(value) > 127)
    {
        return false;
    }

    for (const char * cursor = value; *cursor != '\0'; ++cursor)
    {
        if ((*cursor < 'a' || *cursor > 'z') && (*cursor < 'A' || *cursor > 'Z') &&
            (*cursor < '0' || *cursor > '9') && *cursor != '_' && *cursor != '-' && *cursor != ' ')
        {
            return false;
        }
    }

    return true;
}
} // namespace

const char * CatalogView::String(std::uint32_t offset) const
{
    if (m_bytes == nullptr || offset >= Word(m_bytes, 56))
    {
        return nullptr;
    }

    const auto * strings = m_bytes + Word(m_bytes, 52);
    return offset == 0 || strings[offset - 1] == 0 ? reinterpret_cast<const char *>(strings + offset) : nullptr;
}

std::uint32_t CatalogView::ImageCount() const
{
    return m_bytes != nullptr ? Word(m_bytes, 16) : 0;
}

std::uint32_t CatalogView::FontCount() const
{
    return m_bytes != nullptr ? Word(m_bytes, 20) : 0;
}

std::uint32_t CatalogView::MaterialCount() const
{
    return m_bytes != nullptr ? Word(m_bytes, 28) : 0;
}

bool CatalogView::GetImage(std::uint32_t index, ImageRecord & output) const
{
    if (index >= ImageCount())
    {
        return false;
    }

    const auto offset = Word(m_bytes, 32) + index * kImageBytes;
    output = { String(Word(m_bytes, offset)), String(Word(m_bytes, offset + 4)),
               Word(m_bytes, offset + 8), Word(m_bytes, offset + 12), Word(m_bytes, offset + 16),
               m_bytes + Word(m_bytes, offset + 20), Word(m_bytes, offset + 24) };
    return true;
}

bool CatalogView::GetFont(std::uint32_t index, FontRecord & output) const
{
    if (index >= FontCount())
    {
        return false;
    }

    const auto offset = Word(m_bytes, 36) + index * kFontBytes;
    FontRecord font = { String(Word(m_bytes, offset)), String(Word(m_bytes, offset + 4)), String(Word(m_bytes, offset + 8)), Word(m_bytes, offset + 12), Word(m_bytes, offset + 16), Word(m_bytes, offset + 20), {}, {} };

    for (std::uint32_t i = 0; i < 3; ++i)
    {
        font.maxAdvance[i] = Word(m_bytes, offset + 24 + i * 8);
        font.maxHeight[i] = Word(m_bytes, offset + 28 + i * 8);
    }

    font.index = index;
    font.glyphTable = Word(m_bytes, offset + 48);
    font.glyphTableBytes = Word(m_bytes, offset + 52);
    output = font;
    return true;
}

bool CatalogView::GetGlyphSet(const FontRecord & font, GlyphSetRecord & output) const
{
    if (m_bytes == nullptr || font.glyphTable == 0)
    {
        return false;
    }

    output = { Word(m_bytes, font.glyphTable), Word(m_bytes, font.glyphTable + 4), Word(m_bytes, font.glyphTable + 8) };
    return true;
}

bool CatalogView::GetGlyphSize(const FontRecord & font, std::uint32_t index, GlyphSize & output) const
{
    GlyphSetRecord set;

    if (!GetGlyphSet(font, set) || index >= set.sizeCount)
    {
        return false;
    }

    const auto offset = font.glyphTable + kGlyphSetBytes + index * kGlyphSizeBytes;
    output = { Word(m_bytes, offset), Word(m_bytes, offset + 4), Word(m_bytes, offset + 8), Word(m_bytes, offset + 12),
               Word(m_bytes, offset + 16) };
    return true;
}

bool CatalogView::GetGlyphEntry(const FontRecord & font, std::uint32_t index, GlyphEntry & output) const
{
    GlyphSetRecord set;

    if (!GetGlyphSet(font, set) || index >= set.entryCount)
    {
        return false;
    }

    const auto offset = font.glyphTable + kGlyphSetBytes + set.sizeCount * kGlyphSizeBytes + index * kGlyphEntryBytes;
    output = { Word(m_bytes, offset), Half(m_bytes, offset + 4), (Half(m_bytes, offset + 6) & kGlyphOutlineFlag) != 0,
               SignedHalf(m_bytes, offset + 8), SignedHalf(m_bytes, offset + 10) };
    return true;
}

bool CatalogView::ValidateGlyphSet(const FontRecord & font, std::uint32_t images) const
{
    if (font.glyphTableBytes < kGlyphSetBytes)
    {
        return false;
    }

    const auto sizes = Word(m_bytes, font.glyphTable + 4);
    const auto entries = Word(m_bytes, font.glyphTable + 8);
    ImageRecord image;
    gstx::Header header;

    if (sizes == 0 || sizes > kMaxGlyphSizes || entries == 0 ||
        entries > kMaxGlyphEntries || Word(m_bytes, font.glyphTable + 12) != 0 ||
        font.glyphTableBytes != kGlyphSetBytes + sizes * kGlyphSizeBytes + entries * kGlyphEntryBytes ||
        Word(m_bytes, font.glyphTable) >= images || !GetImage(Word(m_bytes, font.glyphTable), image) ||
        !gstx::DecodeTables(image.descriptor, image.descriptorBytes, header))
    {
        return false;
    }

    // Exact pixels need a 1:1 placement: its match is its source, and its page rectangle the same
    // size. A glyph with an outline splits it into two halves of equal width.
    const auto Bitmap = [&](std::uint16_t placement, bool outline)
    {
        if (placement == kNoGlyphPlacement)
        {
            return !outline;
        }

        if (placement >= header.placementCount)
        {
            return false;
        }

        const auto record = gstx::GetPlacement(image.descriptor, header, placement);
        return record.match.x == record.source.x && record.match.y == record.source.y && record.match.width == record.source.width &&
               record.match.height == record.source.height && record.dest.width == record.match.width &&
               record.dest.height == record.match.height && (!outline || (record.match.width >= 2 && record.match.width % 2 == 0));
    };

    const auto Origin = [](std::int16_t value)
    {
        return value >= -kMaxGlyphOrigin && value <= kMaxGlyphOrigin;
    };

    std::uint32_t next = 0;

    for (std::uint32_t index = 0; index < sizes; ++index)
    {
        GlyphSize size;
        GetGlyphSize(font, index, size);

        if (size.scaleX == 0 || size.scaleX > kMaxGlyphScale || size.scaleY == 0 || size.scaleY > kMaxGlyphScale ||
            size.strokeWeight > kMaxStrokeWeight || size.firstEntry != next || size.entryCount == 0 ||
            size.entryCount > entries - next)
        {
            return false;
        }

        for (std::uint32_t item = 0; item < size.entryCount; ++item)
        {
            GlyphEntry entry;
            GlyphEntry previous;
            GetGlyphEntry(font, size.firstEntry + item, entry);
            const auto entryOffset = font.glyphTable + kGlyphSetBytes + sizes * kGlyphSizeBytes + (size.firstEntry + item) * kGlyphEntryBytes;

            if (entry.codepoint > kMaxCodepoint || (Half(m_bytes, entryOffset + 6) & ~kGlyphOutlineFlag) != 0 ||
                (item != 0 && GetGlyphEntry(font, size.firstEntry + item - 1, previous) && entry.codepoint <= previous.codepoint))
            {
                return false;
            }

            // Only a size with a stroke weight draws outlines.
            if (!Bitmap(entry.placement, entry.outline) || (size.strokeWeight == 0 && entry.outline) || !Origin(entry.originX) ||
                !Origin(entry.originY))
            {
                return false;
            }
        }

        next += size.entryCount;
    }

    return next == entries;
}

bool CatalogView::GetMaterial(std::uint32_t index, MaterialRecord & output) const
{
    if (index >= MaterialCount())
    {
        return false;
    }

    const auto offset = Word(m_bytes, 44) + index * 8;
    output = { String(Word(m_bytes, offset)), Word(m_bytes, offset + 4) };
    return true;
}

bool CatalogView::FindImage(const char * name, ImageRecord & output) const
{
    if (name == nullptr)
    {
        return false;
    }

    for (std::uint32_t i = 0; i < ImageCount(); ++i)
    {
        ImageRecord image;
        GetImage(i, image);

        if (std::strcmp(name, image.name) == 0)
        {
            output = image;
            return true;
        }
    }

    return false;
}

bool CatalogView::FindFont(const char * requested, FontRecord & output) const
{
    if (m_bytes == nullptr || !Alias(requested))
    {
        return false;
    }

    for (std::uint32_t i = 0; i < Word(m_bytes, 24); ++i)
    {
        const auto offset = Word(m_bytes, 40) + i * 8;

        if (std::strcmp(requested, String(Word(m_bytes, offset))) == 0)
        {
            return GetFont(Word(m_bytes, offset + 4), output);
        }
    }

    return false;
}

bool CatalogView::Init(const void * bytes, std::size_t length)
{
    if (bytes == nullptr || length < kHeaderBytes || length > kMaxCatalogBytes)
    {
        return false;
    }

    const auto * data = static_cast<const std::uint8_t *>(bytes);
    const auto images = Word(data, 16);
    const auto fonts = Word(data, 20);
    const auto aliases = Word(data, 24);
    const auto materials = Word(data, 28);

    // Glyph set tables, in font order, fill the space between the materials and the descriptors.
    if (Word(data, 0) != 0x54434955U || Word(data, 4) != kVersion || Word(data, 8) != length || Word(data, 12) != 1 ||
        Word(data, 60) != 0 || images == 0 || images > kMaxImages || fonts == 0 || fonts > kMaxFonts ||
        aliases == 0 || aliases > kMaxAliases || materials > kMaxMaterials || Word(data, 32) != kHeaderBytes ||
        Word(data, 36) != kHeaderBytes + images * kImageBytes || Word(data, 40) != Word(data, 36) + fonts * kFontBytes ||
        Word(data, 44) != Word(data, 40) + aliases * 8 || Word(data, 48) < Word(data, 44) + materials * 8)
    {
        return false;
    }

    const auto glyphSets = Word(data, 44) + materials * 8;
    const auto descriptors = Word(data, 48);
    const auto strings = Word(data, 52);
    const auto stringBytes = Word(data, 56);

    if (descriptors > length || strings < descriptors || strings > length || strings - descriptors > 65536 ||
        stringBytes == 0 || stringBytes > 65536 || stringBytes != length - strings || data[length - 1] != 0)
    {
        return false;
    }

    std::uint32_t run = 0;

    for (std::uint32_t i = strings; i < length; ++i)
    {
        const auto ch = data[i];

        if (ch == 0)
        {
            run = 0;
        }
        else if (ch < 32 || ch > 126 || ++run > 255)
        {
            return false;
        }
    }

    CatalogView candidate;
    candidate.m_bytes = data;
    auto next = descriptors;
    std::uint32_t pages = 0;

    for (std::uint32_t i = 0; i < images; ++i)
    {
        const auto offset = kHeaderBytes + i * kImageBytes;
        const auto start = Word(data, offset + 20);
        const auto size = Word(data, offset + 24);

        if (start != next || size < gstx::kHeaderBytes || size > strings - next || Word(data, offset + 28) != 0)
        {
            return false;
        }

        ImageRecord image;
        candidate.GetImage(i, image);
        // The descriptor is the converted texture's complete header and tables.
        gstx::Header header;

        if (!Path(image.name) || !AssetPath(image.path, ".gstx") || !gstx::DecodeTables(image.descriptor, size, header) ||
            header.fileBytes != image.fileBytes || header.logicalWidth != image.width || header.logicalHeight != image.height)
        {
            return false;
        }

        for (std::uint32_t prior = 0; prior < i; ++prior)
        {
            ImageRecord other;
            candidate.GetImage(prior, other);

            if (std::strcmp(image.name, other.name) == 0)
            {
                return false;
            }
        }

        next += size;
        pages += header.pageCount;
    }

    if (next != strings || pages > 256)
    {
        return false;
    }

    auto glyphCursor = glyphSets;

    for (std::uint32_t i = 0; i < fonts; ++i)
    {
        FontRecord font;
        candidate.GetFont(i, font);

        if (!Family(font.family) || font.logicalPath == nullptr || !FontPath(font.logicalPath, font.family) ||
            !AssetPath(font.path, ".dat") || font.bytes < 26 || font.bytes > ps2::ui::fonts::kMaxFontBytes ||
            (font.bytes - 12) % 14 != 0 || font.imageIndex >= images)
        {
            return false;
        }

        if (font.glyphTable == 0 ? font.glyphTableBytes != 0
                                 : font.glyphTable != glyphCursor || font.glyphTableBytes > descriptors - glyphCursor ||
                                       !candidate.ValidateGlyphSet(font, images))
        {
            return false;
        }

        glyphCursor += font.glyphTableBytes;

        for (int scale = 0; scale < 3; ++scale)
        {
            if (font.maxAdvance[scale] == 0 || font.maxAdvance[scale] > 4096 || font.maxHeight[scale] == 0 || font.maxHeight[scale] > 4096)
            {
                return false;
            }
        }

        for (std::uint32_t prior = 0; prior < i; ++prior)
        {
            FontRecord other;
            candidate.GetFont(prior, other);

            if (std::strcmp(font.family, other.family) == 0)
            {
                return false;
            }
        }
    }

    if (glyphCursor != descriptors)
    {
        return false;
    }

    for (std::uint32_t i = 0; i < aliases; ++i)
    {
        const auto offset = Word(data, 40) + i * 8;
        const char * name = candidate.String(Word(data, offset));

        if (!Alias(name) || Word(data, offset + 4) >= fonts)
        {
            return false;
        }

        for (std::uint32_t prior = 0; prior < i; ++prior)
        {
            const auto previous = Word(data, 40) + prior * 8;

            if (std::strcmp(name, candidate.String(Word(data, previous))) == 0)
            {
                return false;
            }
        }
    }

    for (std::uint32_t i = 0; i < fonts; ++i)
    {
        FontRecord font;
        FontRecord resolved;
        candidate.GetFont(i, font);

        if (!candidate.FindFont(font.family, resolved) || std::strcmp(font.family, resolved.family) != 0)
        {
            return false;
        }
    }

    for (std::uint32_t i = 0; i < materials; ++i)
    {
        MaterialRecord material;
        candidate.GetMaterial(i, material);

        if (!Path(material.name) || material.imageIndex >= images)
        {
            return false;
        }

        for (std::uint32_t prior = 0; prior < i; ++prior)
        {
            MaterialRecord other;
            candidate.GetMaterial(prior, other);

            if (std::strcmp(material.name, other.name) == 0)
            {
                return false;
            }
        }
    }

    *this = candidate;
    return true;
}
} // namespace ps2::ui::retail
