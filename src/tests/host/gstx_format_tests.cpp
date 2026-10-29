// ================================================================================================
// File: gstx_format_tests.cpp
// Brief: Check the target's GSTX admission against files the host converter writes: every table
// field and page must decode identically, and any corrupted table byte must be refused.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/texture_convert.h"
#include "ps2/ui/gstx_format.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

namespace gstx = ps2::ui::gstx;

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "GSTX format failed: %s\n", name);
    }

    return passed;
}

// Three regions with different needs: a translucent gradient (8-bit), flat bands (4-bit) and a
// second gradient that can share the first one's palette.
static assets::RgbaImage Source()
{
    assets::RgbaImage image;
    image.Allocate(160, 96);

    for (int y = 0; y < 96; ++y)
    {
        for (int x = 0; x < 160; ++x)
        {
            std::uint8_t * texel = image.At(x, y);
            const bool flat = x >= 64 && x < 112;
            texel[0] = flat ? 220 : static_cast<std::uint8_t>(x * 2);
            texel[1] = flat ? static_cast<std::uint8_t>(y < 48 ? 20 : 120) : static_cast<std::uint8_t>(y * 2);
            texel[2] = flat ? 40 : static_cast<std::uint8_t>(255 - x);
            texel[3] = flat ? 255 : static_cast<std::uint8_t>(40 + y * 2);
        }
    }

    return image;
}

static bool SameRect(const gstx::Rect & first, const assets::Rect & second)
{
    return first.x == static_cast<std::uint32_t>(second.x) && first.y == static_cast<std::uint32_t>(second.y) &&
           first.width == static_cast<std::uint32_t>(second.width) && first.height == static_cast<std::uint32_t>(second.height);
}

static bool Admission()
{
    const assets::RgbaImage image = Source();
    const std::vector<assets::PlacementRequest> requests = { { { 1, 1, 62, 94 }, { 0, 0, 64, 96 }, 0.0 },
                                                             { { 65, 1, 46, 94 }, { 64, 0, 48, 96 }, 0.0 },
                                                             { { 113, 1, 46, 94 }, { 112, 0, 48, 96 }, 0.0 } };
    assets::ConvertResult result;
    std::vector<std::uint8_t> file;
    assets::GstxSizes sizes;
    std::string error;

    if (!Check(assets::ConvertTexture(image, requests, {}, result, error) && assets::WriteGstx(result.texture, file, sizes, error),
               "host conversion"))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    gstx::Header header = {};
    const auto tableBytes = static_cast<std::uint32_t>(sizes.tableBytes);
    bool passed = Check(gstx::DecodeTables(file.data(), tableBytes, header), "target admits the host file") &&
                  Check(header.fileBytes == file.size() && header.logicalWidth == 160 && header.logicalHeight == 96 &&
                            header.pageCount == result.texture.pages.size() && header.clutCount == result.texture.cluts.size() &&
                            header.placementCount == 3,
                        "header fields");

    for (std::uint32_t index = 0; passed && index < header.pageCount; ++index)
    {
        const gstx::Page page = gstx::GetPage(file.data(), index);
        const assets::GstxPage & expected = result.texture.pages[index];
        std::vector<std::uint8_t> decoded(page.decodedBytes);
        const bool unpacked = page.rle ? gstx::RleDecode(file.data() + page.dataOffset, page.dataBytes, decoded.data(), decoded.size())
                                       : (decoded.assign(file.begin() + page.dataOffset, file.begin() + page.dataOffset + page.dataBytes), true);
        passed = Check(page.width == static_cast<std::uint32_t>(expected.width) && page.height == static_cast<std::uint32_t>(expected.height) &&
                           page.psm == static_cast<std::uint8_t>(expected.format) && unpacked && decoded == expected.data,
                       "page records and data decode identically");
    }

    for (std::uint32_t index = 0; passed && index < header.placementCount; ++index)
    {
        const gstx::Placement placement = gstx::GetPlacement(file.data(), header, index);
        const assets::GstxPlacement & expected = result.texture.placements[index];
        passed = Check(SameRect(placement.match, expected.match) && SameRect(placement.source, expected.source) &&
                           SameRect(placement.dest, expected.dest) && placement.page == static_cast<std::uint32_t>(expected.page) &&
                           placement.clut == static_cast<std::uint32_t>(expected.clut),
                       "placement records");
    }

    for (std::uint32_t index = 0; passed && index < header.clutCount; ++index)
    {
        const gstx::Clut clut = gstx::GetClut(file.data(), header, index);
        passed = Check(clut.entries == result.texture.cluts[index].texels.size(), "CLUT records");
    }

    // Every single-byte corruption of the header or tables is refused.
    for (std::uint32_t offset = 0; passed && offset < tableBytes; ++offset)
    {
        std::vector<std::uint8_t> damaged(file.begin(), file.begin() + tableBytes);
        damaged[offset] ^= 0x10;
        gstx::Header ignored = {};
        passed = Check(!gstx::DecodeTables(damaged.data(), tableBytes, ignored), "corrupted table byte refused");
    }

    gstx::Header ignored = {};
    passed = passed && Check(!gstx::DecodeTables(file.data(), tableBytes - 1U, ignored), "short table refused") &&
             Check(!gstx::DecodeTables(nullptr, tableBytes, ignored), "null tables refused");
    std::uint8_t output[4] = {};
    const std::uint8_t run[] = { 129, 7 };
    const std::uint8_t literal[] = { 1, 1, 2 };
    passed = passed && Check(gstx::RleDecode(run, sizeof(run), output, 4) && output[3] == 7, "run decodes") &&
             Check(!gstx::RleDecode(run, sizeof(run), output, 3), "run overflow refused") &&
             Check(!gstx::RleDecode(literal, 2, output, 2), "truncated literal refused") &&
             Check(!gstx::RleDecode(literal, sizeof(literal), output, 3), "short output refused");
    return passed;
}

static bool Checksums()
{
    static constexpr char kA[] = "a";
    static constexpr char kFoobar[] = "foobar";
    static constexpr char kHello[] = "hello";
    return Check(gstx::Checksum(nullptr, 0) == 0x811C9DC5, "empty FNV-1a32 basis") &&
           Check(gstx::Checksum(kA, sizeof(kA) - 1U) == 0xE40C292C, "FNV-1a32 a vector") &&
           Check(gstx::Checksum(kFoobar, sizeof(kFoobar) - 1U) == 0xBF9CF968, "FNV-1a32 foobar vector") &&
           Check(gstx::Checksum(kHello, sizeof(kHello) - 1U) == 0x4F9F2CAB, "FNV-1a32 hello vector");
}

} // namespace

int main()
{
    const bool checksums = Checksums();

    if (!Admission() || !checksums)
    {
        return 1;
    }

    std::puts("GSTX format tests passed.");
    return 0;
}
