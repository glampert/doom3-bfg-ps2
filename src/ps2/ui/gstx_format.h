// ================================================================================================
// File: gstx_format.h
// Brief: Validate converted GSTX texture headers and tables before any pixel read or allocation,
// and decode their RLE pages, so a malformed or tampered file is refused at admission.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui::gstx
{

// The layout is defined by src/tools/assets/gstx.h, which writes it.
static constexpr std::uint32_t kMagic = 0x58545347;
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kHeaderBytes = 48;
static constexpr std::uint32_t kPageRecordBytes = 32;
static constexpr std::uint32_t kClutRecordBytes = 16;
static constexpr std::uint32_t kPlacementRecordBytes = 32;
static constexpr std::uint32_t kMaxPages = 16;
static constexpr std::uint32_t kMaxCluts = 128;
static constexpr std::uint32_t kMaxPlacements = 1024;
static constexpr std::uint32_t kMaxPageExtent = 512;
static constexpr std::uint32_t kMaxLogicalExtent = 16384;
static constexpr std::uint32_t kMaxFileBytes = 16U * 1024U * 1024U;
static constexpr std::uint32_t kMaxTableBytes = kHeaderBytes + kMaxPages * kPageRecordBytes + kMaxCluts * kClutRecordBytes + kMaxPlacements * kPlacementRecordBytes;
static constexpr std::uint8_t kPsmT8 = 0x13;
static constexpr std::uint8_t kPsmT4 = 0x14;

struct Header
{
    std::uint32_t logicalWidth;
    std::uint32_t logicalHeight;
    std::uint32_t pageCount;
    std::uint32_t clutCount;
    std::uint32_t placementCount;
    std::uint32_t tableBytes;
    std::uint32_t fileBytes;
};

struct Page
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t psm;
    bool rle;
    std::uint32_t dataOffset;
    std::uint32_t dataBytes;
    std::uint32_t decodedBytes;
    std::uint32_t checksum;
};

struct Clut
{
    std::uint32_t entries;
    std::uint32_t dataOffset;
    std::uint32_t dataBytes;
    std::uint32_t checksum;
};

struct Rect
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
};

struct Placement
{
    Rect match;
    Rect source;
    Rect dest;
    std::uint32_t page;
    std::uint32_t clut;
};

// Validates the complete header and tables in `bytes` (exactly the file's first tableBytes):
// counts, the table checksum, every record, the canonical data offsets that must follow, page
// formats and sizes, CLUT formats, and placement rectangles (matches disjoint). Invalid input
// leaves the output unchanged.
bool DecodeTables(const std::uint8_t * bytes, std::uint32_t length, Header & output);

// Records of already validated tables.
Page GetPage(const std::uint8_t * tables, std::uint32_t index);
Clut GetClut(const std::uint8_t * tables, const Header & header, std::uint32_t index);
Placement GetPlacement(const std::uint8_t * tables, const Header & header, std::uint32_t index);

// Decodes exactly outputBytes, consuming every input byte; false otherwise.
bool RleDecode(const std::uint8_t * input, std::size_t inputBytes, std::uint8_t * output, std::size_t outputBytes);

// Standard FNV-1a32, which pages, CLUTs and the catalog's admitted files record; zero length does
// not access the buffer.
std::uint32_t Checksum(const void * bytes, std::size_t length);

} // namespace ps2::ui::gstx
