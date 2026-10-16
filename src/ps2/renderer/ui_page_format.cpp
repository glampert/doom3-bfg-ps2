// ================================================================================================
// File: ui_page_format.cpp
// Brief: Decode UI page integers without trusting packed layouts, host endian order or file counts.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_page_format.h"

namespace ps2::ui
{
namespace
{

static constexpr std::uint32_t kPageMagic = 0x47504955;
static constexpr std::uint32_t kPageVersion = 1;
static constexpr std::uint32_t kMaxImageExtent = 16384;
static constexpr std::uint64_t kMaxImageRgbaBytes = 64 * 1024 * 1024;
static constexpr std::uint64_t kMaxNativeFileBytes = 0x7FFFFFFF;
static constexpr std::uint32_t kFnvBasis = 2166136261;
static constexpr std::uint32_t kFnvPrime = 16777619;

static std::uint32_t ReadLittleWord(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

static bool DecodeLayout(std::uint32_t width, std::uint32_t height, std::uint32_t pageCount,
                         std::uint32_t fileBytes, PageHeader & output)
{
    if (width == 0 || height == 0 || width > kMaxImageExtent || height > kMaxImageExtent ||
        pageCount == 0 || pageCount > kMaxImagePages ||
        static_cast<std::uint64_t>(width) * height * 4U > kMaxImageRgbaBytes)
    {
        return false;
    }

    const std::uint32_t columns = (width + kPageInterior - 1U) / kPageInterior;
    const std::uint32_t rows = (height + kPageInterior - 1U) / kPageInterior;
    const std::uint64_t inferredCount = static_cast<std::uint64_t>(columns) * rows;
    const std::uint64_t dataOffset = kPageHeaderBytes + static_cast<std::uint64_t>(pageCount) * kPageRecordBytes;
    const std::uint64_t expectedBytes = dataOffset + static_cast<std::uint64_t>(pageCount) * kPageBytes;
    if (inferredCount != pageCount || expectedBytes > kMaxNativeFileBytes || expectedBytes != fileBytes)
    {
        return false;
    }

    output = { width, height, columns, rows, pageCount, static_cast<std::uint32_t>(dataOffset), fileBytes };
    return true;
}

} // namespace

bool DecodePageHeader(const std::uint8_t (&bytes)[32], std::uint32_t fileBytes, PageHeader & output)
{
    if (ReadLittleWord(bytes) != kPageMagic || ReadLittleWord(bytes + 4) != kPageVersion ||
        ReadLittleWord(bytes + 16) != kPageExtent || ReadLittleWord(bytes + 20) != kPageGutter ||
        ReadLittleWord(bytes + 28) != 0)
    {
        return false;
    }

    return DecodeLayout(ReadLittleWord(bytes + 8), ReadLittleWord(bytes + 12), ReadLittleWord(bytes + 24),
                        fileBytes, output);
}

bool DecodePageRecord(const std::uint8_t (&bytes)[8], const PageHeader & header, std::uint32_t index,
                      PageRecord & output)
{
    PageHeader expected{};
    if (!DecodeLayout(header.width, header.height, header.pageCount, header.fileBytes, expected) ||
        expected.columns != header.columns || expected.rows != header.rows || expected.dataOffset != header.dataOffset ||
        index >= header.pageCount)
    {
        return false;
    }

    const std::uint64_t expectedOffset = header.dataOffset + static_cast<std::uint64_t>(index) * kPageBytes;
    const std::uint32_t offset = ReadLittleWord(bytes);
    if (offset != expectedOffset)
    {
        return false;
    }

    output = { offset, ReadLittleWord(bytes + 4) };
    return true;
}

std::uint32_t PageChecksum(const void * bytes, std::size_t length)
{
    const auto * const data = static_cast<const std::uint8_t *>(bytes);
    std::uint32_t checksum = kFnvBasis;
    for (std::size_t index = 0; index < length; ++index)
    {
        checksum = (checksum ^ static_cast<std::uint32_t>(data[index])) * kFnvPrime;
    }

    return checksum;
}

} // namespace ps2::ui
