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

private:
    const char * String(std::uint32_t offset) const;
    const std::uint8_t * m_bytes = nullptr;
};
} // namespace ps2::ui::retail
