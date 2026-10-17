// ================================================================================================
// File: retail_catalog.cpp
// Brief: Reject malformed catalog offsets, identities and page descriptors before native allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_catalog.h"
#include "ps2/renderer/ui_font_format.h"
#include "ps2/renderer/ui_page_format.h"

#include <cstring>

namespace ps2::ui::retail
{
namespace
{
static constexpr std::uint32_t kHeaderBytes = 64;
static constexpr std::uint32_t kImageBytes = 32;
static constexpr std::uint32_t kFontBytes = 56;

std::uint32_t Word(const std::uint8_t * bytes, std::uint32_t offset)
{
    const auto * p = bytes + offset;
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
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

bool AssetPath(const char * value, const char * suffix)
{
    if (value == nullptr || std::strlen(value) != 75 || std::strncmp(value, "assets/", 7) != 0 ||
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

    output = font;
    return true;
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

    if (Word(data, 0) != 0x54434955U || Word(data, 4) != 1 || Word(data, 8) != length || Word(data, 12) != 1 ||
        Word(data, 60) != 0 || images == 0 || images > kMaxImages || fonts == 0 || fonts > kMaxFonts ||
        aliases == 0 || aliases > kMaxAliases || materials > kMaxMaterials || Word(data, 32) != kHeaderBytes ||
        Word(data, 36) != kHeaderBytes + images * kImageBytes || Word(data, 40) != Word(data, 36) + fonts * kFontBytes ||
        Word(data, 44) != Word(data, 40) + aliases * 8 || Word(data, 48) != Word(data, 44) + materials * 8)
    {
        return false;
    }

    const auto descriptors = Word(data, 48);
    const auto strings = Word(data, 52);
    const auto stringBytes = Word(data, 56);

    if (descriptors > length || strings < descriptors || strings > length || strings - descriptors > 16384 ||
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

        if (start != next || size < kPageHeaderBytes + kPageRecordBytes || size > strings - next || Word(data, offset + 28) != 0)
        {
            return false;
        }

        ImageRecord image;
        candidate.GetImage(i, image);
        PageHeader header;
        std::uint8_t rawHeader[kPageHeaderBytes];
        std::memcpy(rawHeader, image.descriptor, sizeof(rawHeader));

        if (!Path(image.name) || !AssetPath(image.path, ".uip") || !DecodePageHeader(rawHeader, image.fileBytes, header) ||
            header.width != image.width || header.height != image.height || size != kPageHeaderBytes + header.pageCount * kPageRecordBytes)
        {
            return false;
        }

        for (std::uint32_t page = 0; page < header.pageCount; ++page)
        {
            PageRecord record;
            std::uint8_t rawRecord[kPageRecordBytes];
            std::memcpy(rawRecord, image.descriptor + kPageHeaderBytes + page * kPageRecordBytes, sizeof(rawRecord));

            if (!DecodePageRecord(rawRecord, header, page, record))
            {
                return false;
            }
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

    if (next != strings || pages > 1024)
    {
        return false;
    }

    for (std::uint32_t i = 0; i < fonts; ++i)
    {
        const auto offset = Word(data, 36) + i * kFontBytes;
        FontRecord font;
        candidate.GetFont(i, font);

        if (!Family(font.family) || font.logicalPath == nullptr || !FontPath(font.logicalPath, font.family) ||
            !AssetPath(font.path, ".dat") || font.bytes < 26 || font.bytes > ps2::fonts::kMaxFontBytes ||
            (font.bytes - 12) % 14 != 0 || font.imageIndex >= images || Word(data, offset + 48) != 0 || Word(data, offset + 52) != 0)
        {
            return false;
        }

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
