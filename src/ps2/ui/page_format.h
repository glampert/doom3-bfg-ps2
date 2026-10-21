// ================================================================================================
// File: page_format.h
// Brief: Validate bounded cooked UI page metadata before native file reads or allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui
{

static constexpr std::uint32_t kPageExtent = 256;
static constexpr std::uint32_t kPageGutter = 1;
static constexpr std::uint32_t kPageInterior = 254;
static constexpr std::uint32_t kPageBytes = 262144;
static constexpr std::uint32_t kPageHeaderBytes = 32;
static constexpr std::uint32_t kPageRecordBytes = 8;
static constexpr std::uint32_t kMaxImagePages = 1024;

struct PageHeader
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t columns;
    std::uint32_t rows;
    std::uint32_t pageCount;
    std::uint32_t dataOffset;
    std::uint32_t fileBytes;
};

struct PageRecord
{
    std::uint32_t offset;
    std::uint32_t checksum;
};

// UIPG v1 uses little-endian words and fixed 256x256 RGBA pages with one-pixel gutters.
// The complete file must contain exactly its header, inferred record table and all page pixels.
// Invalid input leaves output unchanged; records also require a consistent decoded header.
bool DecodePageHeader(const std::uint8_t (&bytes)[32], std::uint32_t fileBytes, PageHeader & output);
bool DecodePageRecord(const std::uint8_t (&bytes)[8], const PageHeader & header, std::uint32_t index,
                      PageRecord & output);

// Standard FNV-1a32 over a valid caller-owned buffer; zero length does not access the buffer.
std::uint32_t PageChecksum(const void * bytes, std::size_t length);

} // namespace ps2::ui
