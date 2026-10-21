// ================================================================================================
// File: ui_page_format_tests.cpp
// Brief: Exercise cooked page metadata bounds and checksums independently of file IO or the GS.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/page_format.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace
{

using ps2::ui::DecodePageHeader;
using ps2::ui::DecodePageRecord;
using ps2::ui::PageChecksum;
using ps2::ui::PageHeader;
using ps2::ui::PageRecord;

static constexpr PageHeader kHeaderSentinel{ 91, 92, 93, 94, 95, 96, 97 };
static constexpr PageRecord kRecordSentinel{ 0x12345678, 0xABCDEF01 };

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI page format failed: %s\n", name);
    }

    return passed;
}

static bool Equal(const PageHeader & first, const PageHeader & second)
{
    return first.width == second.width && first.height == second.height && first.columns == second.columns &&
           first.rows == second.rows && first.pageCount == second.pageCount && first.dataOffset == second.dataOffset &&
           first.fileBytes == second.fileBytes;
}

static bool Equal(const PageRecord & first, const PageRecord & second)
{
    return first.offset == second.offset && first.checksum == second.checksum;
}

static void WriteLittleWord(std::uint8_t * bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
    bytes[2] = static_cast<std::uint8_t>(value >> 16U);
    bytes[3] = static_cast<std::uint8_t>(value >> 24U);
}

static std::uint32_t HeaderBytes(std::uint8_t (&bytes)[32], std::uint32_t width, std::uint32_t height)
{
    const std::uint32_t columns = (width + 253U) / 254U;
    const std::uint32_t rows = (height + 253U) / 254U;
    const std::uint32_t count = columns * rows;
    const std::uint32_t words[] = { 0x47504955, 1, width, height, 256, 1, count, 0 };
    for (std::size_t index = 0; index < 8; ++index)
    {
        WriteLittleWord(bytes + index * 4, words[index]);
    }

    return 32U + 8U * count + 262144U * count;
}

static bool HeaderRejected(const std::uint8_t (&bytes)[32], std::uint32_t fileBytes)
{
    PageHeader output = kHeaderSentinel;
    return !DecodePageHeader(bytes, fileBytes, output) && Equal(output, kHeaderSentinel);
}

static bool RecordRejected(const std::uint8_t (&bytes)[8], const PageHeader & header, std::uint32_t index)
{
    PageRecord output = kRecordSentinel;
    return !DecodePageRecord(bytes, header, index, output) && Equal(output, kRecordSentinel);
}

static bool ValidHeaders()
{
    struct Case
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t columns;
        std::uint32_t rows;
    };

    static constexpr Case kCases[] = {
        { 1, 1, 1, 1 },
        { 254, 254, 1, 1 },
        { 255, 255, 2, 2 },
        { 640, 480, 3, 2 },
        { 1408, 2320, 6, 10 },
        { 512, 640, 3, 3 },
        { 16384, 1, 65, 1 },
        { 1, 16384, 1, 65 },
        { 16384, 1024, 65, 5 },
        { 8192, 2048, 33, 9 }
    };

    for (const Case & test : kCases)
    {
        std::uint8_t bytes[32]{};
        const std::uint32_t fileBytes = HeaderBytes(bytes, test.width, test.height);
        PageHeader output = kHeaderSentinel;
        const std::uint32_t count = test.columns * test.rows;
        const PageHeader expected{ test.width, test.height, test.columns, test.rows, count, 32U + count * 8U, fileBytes };
        if (!Check(DecodePageHeader(bytes, fileBytes, output) && Equal(output, expected), "valid NPOT and maximum layouts"))
        {
            return false;
        }
    }

    return true;
}

static bool InvalidHeaders()
{
    struct Change
    {
        std::size_t word;
        std::uint32_t value;
    };

    static constexpr Change kChanges[] = {
        { 0, 0 },
        { 0, 0x55495047 },
        { 1, 0 },
        { 1, 2 },
        { 2, 0 },
        { 2, 16385 },
        { 2, 0xFFFFFFFF },
        { 3, 0 },
        { 3, 16385 },
        { 3, 0xFFFFFFFF },
        { 4, 255 },
        { 4, 512 },
        { 5, 0 },
        { 5, 2 },
        { 6, 0 },
        { 6, 1 },
        { 6, 1024 },
        { 6, 1025 },
        { 6, 0xFFFFFFFF },
        { 7, 1 },
        { 7, 0xFFFFFFFF }
    };

    for (const Change & change : kChanges)
    {
        std::uint8_t bytes[32]{};
        const std::uint32_t fileBytes = HeaderBytes(bytes, 640, 480);
        WriteLittleWord(bytes + change.word * 4, change.value);
        if (!Check(HeaderRejected(bytes, fileBytes), "bad magic/version/extent/gutter/count/reserved"))
        {
            return false;
        }
    }

    std::uint8_t bytes[32]{};
    const std::uint32_t fileBytes = HeaderBytes(bytes, 640, 480);
    const std::uint32_t wrongLengths[] = { 0, 31, fileBytes - 1U, fileBytes + 1U, 0x7FFFFFFF, 0xFFFFFFFF };
    for (const std::uint32_t length : wrongLengths)
    {
        if (!Check(HeaderRejected(bytes, length), "exact file length and signed native range"))
        {
            return false;
        }
    }

    const std::uint32_t tooMuchRgba = HeaderBytes(bytes, 16384, 1025);
    if (!Check(HeaderRejected(bytes, tooMuchRgba), "base RGBA limit before page padding"))
    {
        return false;
    }

    const std::uint32_t tooManyPages = HeaderBytes(bytes, 16384, 16384);
    return Check(HeaderRejected(bytes, tooManyPages), "largest dimensions cannot bypass image/page budgets");
}

static bool Records()
{
    std::uint8_t headerBytes[32]{};
    const std::uint32_t fileBytes = HeaderBytes(headerBytes, 640, 480);
    PageHeader header{};
    if (!Check(DecodePageHeader(headerBytes, fileBytes, header), "record test header"))
    {
        return false;
    }

    std::uint8_t recordBytes[8]{};
    for (std::uint32_t index = 0; index < header.pageCount; ++index)
    {
        const std::uint32_t offset = header.dataOffset + index * ps2::ui::kPageBytes;
        const std::uint32_t checksum = index == 0 ? 0 : 0xFFFFFFFF;
        WriteLittleWord(recordBytes, offset);
        WriteLittleWord(recordBytes + 4, checksum);
        PageRecord output = kRecordSentinel;
        if (!Check(DecodePageRecord(recordBytes, header, index, output) && Equal(output, { offset, checksum }),
                   "every page offset and arbitrary unsigned checksum"))
        {
            return false;
        }

        WriteLittleWord(recordBytes, offset + 1U);
        if (!Check(RecordRejected(recordBytes, header, index), "record offset cannot shift or overlap pages"))
        {
            return false;
        }
    }

    WriteLittleWord(recordBytes, header.dataOffset);
    if (!Check(RecordRejected(recordBytes, header, header.pageCount), "index equals page count") ||
        !Check(RecordRejected(recordBytes, header, 0xFFFFFFFF), "hostile unsigned page index"))
    {
        return false;
    }

    PageHeader forged[] = { header, header, header, header, header, header, header };
    forged[0].width = 0;
    forged[1].height = 0xFFFFFFFF;
    forged[2].columns += 1;
    forged[3].rows += 1;
    forged[4].pageCount = 0xFFFFFFFF;
    forged[5].dataOffset = 0xFFFFFFF0;
    forged[6].fileBytes += 1;
    for (const PageHeader & invalid : forged)
    {
        if (!Check(RecordRejected(recordBytes, invalid, 0), "forged header leaves record unpublished"))
        {
            return false;
        }
    }

    return true;
}

static bool Checksums()
{
    static constexpr char kA[] = "a";
    static constexpr char kFoobar[] = "foobar";
    static constexpr char kHello[] = "hello";
    return Check(PageChecksum(nullptr, 0) == 0x811C9DC5, "empty FNV-1a32 basis") &&
           Check(PageChecksum(kA, sizeof(kA) - 1U) == 0xE40C292C, "FNV-1a32 a vector") &&
           Check(PageChecksum(kFoobar, sizeof(kFoobar) - 1U) == 0xBF9CF968, "FNV-1a32 foobar vector") &&
           Check(PageChecksum(kHello, sizeof(kHello) - 1U) == 0x4F9F2CAB, "FNV-1a32 hello vector");
}

} // namespace

int main()
{
    if (!ValidHeaders() || !InvalidHeaders() || !Records() || !Checksums())
    {
        return 1;
    }

    std::puts("UI page format tests passed.");
    return 0;
}
