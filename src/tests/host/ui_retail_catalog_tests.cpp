// ================================================================================================
// File: ui_retail_catalog_tests.cpp
// Brief: Exercise native catalog admission, safe lookups and transactional failure with owned data.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/font_format.h"
#include "ps2/ui/gstx_format.h"
#include "ps2/ui/page_format.h"
#include "ps2/ui/retail_catalog.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

using ps2::ui::retail::CatalogView;
using ps2::ui::retail::FontRecord;
using ps2::ui::retail::ImageRecord;
using ps2::ui::retail::MaterialRecord;

static constexpr std::uint32_t kHeaderBytes = 64;
static constexpr std::uint32_t kImageBytes = 32;
static constexpr std::uint32_t kFontBytes = 56;
static constexpr char kSentinel[] = "unchanged";

struct Fixture
{
    std::vector<std::uint8_t> bytes;
    std::uint32_t fonts;
    std::uint32_t aliases;
    std::uint32_t materials;
    std::uint32_t descriptors;
    std::uint32_t strings;
};

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI retail catalog failed: %s\n", name);
    }

    return passed;
}

static std::uint32_t Word(const std::vector<std::uint8_t> & bytes, std::uint32_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

static void Put(std::vector<std::uint8_t> & bytes, std::uint32_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

static std::uint32_t AppendString(Fixture & fixture, const std::string & text)
{
    const auto offset = static_cast<std::uint32_t>(fixture.bytes.size()) - fixture.strings;
    fixture.bytes.insert(fixture.bytes.end(), text.begin(), text.end());
    fixture.bytes.push_back(0);
    Put(fixture.bytes, 8, static_cast<std::uint32_t>(fixture.bytes.size()));
    Put(fixture.bytes, 56, static_cast<std::uint32_t>(fixture.bytes.size()) - fixture.strings);
    return offset;
}

static void PutHalf(std::vector<std::uint8_t> & bytes, std::uint32_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

// A canonical GSTX header and tables: `pages` raw 128x128 PSMT4 pages, one 16-entry CLUT and one
// placement covering the logical image. Returns the file length the tables describe.
static std::uint32_t AppendDescriptor(std::vector<std::uint8_t> & bytes, std::uint32_t width, std::uint32_t height, std::uint32_t pages)
{
    namespace gstx = ps2::ui::gstx;
    const auto start = static_cast<std::uint32_t>(bytes.size());
    const std::uint32_t tableBytes = gstx::kHeaderBytes + pages * gstx::kPageRecordBytes + gstx::kClutRecordBytes + gstx::kPlacementRecordBytes;
    bytes.resize(start + tableBytes, 0);
    const std::uint32_t clutOffset = (tableBytes + 15U) & ~15U;
    std::uint32_t end = clutOffset + 64U;

    for (std::uint32_t page = 0; page < pages; ++page)
    {
        const std::uint32_t record = start + gstx::kHeaderBytes + page * gstx::kPageRecordBytes;
        const std::uint32_t offset = (end + 15U) & ~15U;
        PutHalf(bytes, record, 128);
        PutHalf(bytes, record + 2U, 128);
        bytes[record + 4U] = gstx::kPsmT4;
        Put(bytes, record + 8U, offset);
        Put(bytes, record + 12U, 8192);
        Put(bytes, record + 16U, 8192);
        Put(bytes, record + 20U, 0x12345678U + page);
        end = offset + 8192U;
    }

    const std::uint32_t clut = start + gstx::kHeaderBytes + pages * gstx::kPageRecordBytes;
    PutHalf(bytes, clut, 16);
    Put(bytes, clut + 4U, clutOffset);
    Put(bytes, clut + 8U, 64);
    const std::uint32_t placement = clut + gstx::kClutRecordBytes;
    PutHalf(bytes, placement + 4U, width);
    PutHalf(bytes, placement + 6U, height);
    PutHalf(bytes, placement + 12U, width);
    PutHalf(bytes, placement + 14U, height);
    PutHalf(bytes, placement + 20U, width < 128U ? width : 128U);
    PutHalf(bytes, placement + 22U, height < 128U ? height : 128U);
    const std::uint32_t fileBytes = (end + 15U) & ~15U;
    Put(bytes, start, gstx::kMagic);
    PutHalf(bytes, start + 4U, gstx::kVersion);
    PutHalf(bytes, start + 6U, gstx::kHeaderBytes);
    PutHalf(bytes, start + 8U, width);
    PutHalf(bytes, start + 10U, height);
    PutHalf(bytes, start + 12U, pages);
    PutHalf(bytes, start + 14U, 1);
    PutHalf(bytes, start + 16U, 1);
    Put(bytes, start + 20U, tableBytes);
    Put(bytes, start + 24U, fileBytes);
    // The checksum covers the header and tables with its own field still zero.
    Put(bytes, start + 28U, ps2::ui::PageChecksum(bytes.data() + start, tableBytes));
    return fileBytes;
}

static Fixture MakeFixture(std::uint32_t imageCount = 2, bool pagePressure = false)
{
    const std::uint32_t fonts = kHeaderBytes + imageCount * kImageBytes;
    const std::uint32_t aliases = fonts + 2U * kFontBytes;
    const std::uint32_t materials = aliases + 5U * 8U;
    const std::uint32_t descriptors = materials + 2U * 8U;
    Fixture fixture{ std::vector<std::uint8_t>(descriptors), fonts, aliases, materials, descriptors, 0 };
    std::vector<std::uint8_t> strings;
    std::map<std::string, std::uint32_t> interned;
    auto intern = [&](const std::string & value) -> std::uint32_t
    {
        const auto existing = interned.find(value);
        if (existing != interned.end())
        {
            return existing->second;
        }

        const auto offset = static_cast<std::uint32_t>(strings.size());
        strings.insert(strings.end(), value.begin(), value.end());
        strings.push_back(0);
        interned.emplace(value, offset);
        return offset;
    };

    for (std::uint32_t index = 0; index < imageCount; ++index)
    {
        const std::uint32_t offset = kHeaderBytes + index * kImageBytes;
        const std::string name = index == 0 ? "newfonts/arial_narrow/48.tga" : index == 1 ? "newfonts/chainlink_semi_bold/48.tga"
                                                                                          : "textures/extra" + std::to_string(index);
        const std::string path = "assets/" + std::string(64, static_cast<char>('a' + index % 6U)) + ".gstx";
        const std::uint32_t width = index == 1 ? 255U : 128U;
        const std::uint32_t height = 128U;
        // Sixteen pages per image, the GSTX maximum, push 17 images past the 256-page total.
        const std::uint32_t pages = pagePressure ? 16U : 1U;
        const auto start = static_cast<std::uint32_t>(fixture.bytes.size());
        const std::uint32_t fileBytes = AppendDescriptor(fixture.bytes, width, height, pages);
        Put(fixture.bytes, offset, intern(name));
        Put(fixture.bytes, offset + 4U, intern(path));
        Put(fixture.bytes, offset + 8U, width);
        Put(fixture.bytes, offset + 12U, height);
        Put(fixture.bytes, offset + 16U, fileBytes);
        Put(fixture.bytes, offset + 20U, start);
        Put(fixture.bytes, offset + 24U, static_cast<std::uint32_t>(fixture.bytes.size()) - start);
    }

    static constexpr const char * kFamilies[] = { "arial_narrow", "chainlink_semi_bold" };
    static constexpr std::uint32_t kMetrics[2][6] = { { 48, 56, 42, 48, 40, 48 }, { 56, 52, 54, 46, 52, 45 } };
    for (std::uint32_t index = 0; index < 2; ++index)
    {
        const std::uint32_t offset = fonts + index * kFontBytes;
        const std::string family = kFamilies[index];
        const std::string path = "assets/" + std::string(64, static_cast<char>('c' + index)) + ".dat";
        Put(fixture.bytes, offset, intern(family));
        Put(fixture.bytes, offset + 4U, intern("newfonts/" + family + "/48.dat"));
        Put(fixture.bytes, offset + 8U, intern(path));
        Put(fixture.bytes, offset + 12U, index == 0 ? 26U : 3092U);
        Put(fixture.bytes, offset + 16U, 0xF0123456U + index);
        Put(fixture.bytes, offset + 20U, index);
        for (std::uint32_t metric = 0; metric < 6; ++metric)
        {
            Put(fixture.bytes, offset + 24U + metric * 4U, kMetrics[index][metric]);
        }
    }

    static constexpr const char * kAliases[] = { "Arial_Narrow", "arial_narrow", "Chainlink_Semi-Bold",
                                                 "chainlink_semi_bold", "" };
    for (std::uint32_t index = 0; index < 5; ++index)
    {
        Put(fixture.bytes, aliases + index * 8U, intern(kAliases[index]));
        Put(fixture.bytes, aliases + index * 8U + 4U, index < 2 ? 0U : 1U);
    }

    for (std::uint32_t index = 0; index < 2; ++index)
    {
        Put(fixture.bytes, materials + index * 8U, intern("newfonts/" + std::string(kFamilies[index]) + "/48"));
        Put(fixture.bytes, materials + index * 8U + 4U, index);
    }

    fixture.strings = static_cast<std::uint32_t>(fixture.bytes.size());
    fixture.bytes.insert(fixture.bytes.end(), strings.begin(), strings.end());
    const std::uint32_t header[] = {
        0x54434955, 2, static_cast<std::uint32_t>(fixture.bytes.size()), 1, imageCount, 2, 5, 2,
        kHeaderBytes, fonts, aliases, materials, descriptors, fixture.strings, static_cast<std::uint32_t>(strings.size()), 0
    };
    for (std::uint32_t word = 0; word < 16; ++word)
    {
        Put(fixture.bytes, word * 4U, header[word]);
    }

    return fixture;
}

static bool UnchangedQueries(const CatalogView & view)
{
    ImageRecord image{ kSentinel, kSentinel, 101, 102, 103, nullptr, 104 };
    FontRecord font{ kSentinel, kSentinel, kSentinel, 101, 102, 103, { 104, 105, 106 }, { 107, 108, 109 } };
    MaterialRecord material{ kSentinel, 101 };
    if (view.ImageCount() == 0 &&
        (view.GetImage(0, image) || view.GetFont(0, font) || view.GetMaterial(0, material)))
    {
        return false;
    }

    return !view.GetImage(0xFFFFFFFFU, image) && !view.FindImage(nullptr, image) &&
           !view.FindImage("missing/image", image) && image.name == kSentinel && image.width == 101 &&
           image.path == kSentinel && image.height == 102 && image.fileBytes == 103 && image.descriptor == nullptr &&
           image.descriptorBytes == 104 && !view.GetFont(0xFFFFFFFFU, font) && !view.FindFont(nullptr, font) &&
           !view.FindFont("not admitted", font) && !view.FindFont("fonts/arial_narrow", font) &&
           font.family == kSentinel && font.logicalPath == kSentinel && font.path == kSentinel && font.bytes == 101 &&
           font.checksum == 102 && font.imageIndex == 103 && font.maxAdvance[0] == 104 && font.maxAdvance[1] == 105 &&
           font.maxAdvance[2] == 106 && font.maxHeight[0] == 107 && font.maxHeight[1] == 108 && font.maxHeight[2] == 109 &&
           !view.GetMaterial(0xFFFFFFFFU, material) && material.name == kSentinel && material.imageIndex == 101;
}

static bool BaselineQueries(const CatalogView & view)
{
    ImageRecord image{};
    FontRecord font{};
    MaterialRecord material{};
    return view.ImageCount() == 2 && view.FontCount() == 2 && view.MaterialCount() == 2 &&
           view.GetImage(1, image) && std::strcmp(image.name, "newfonts/chainlink_semi_bold/48.tga") == 0 &&
           image.width == 255 && image.height == 128 && image.descriptorBytes == 128 &&
           view.GetMaterial(0, material) && material.imageIndex == 0 &&
           std::strcmp(material.name, "newfonts/arial_narrow/48") == 0 &&
           view.FindFont("Arial_Narrow", font) && std::strcmp(font.family, "arial_narrow") == 0 &&
           font.maxAdvance[0] == 48 && font.maxAdvance[1] == 42 && font.maxAdvance[2] == 40 &&
           font.maxHeight[0] == 56 && font.maxHeight[1] == 48 && font.maxHeight[2] == 48 &&
           view.FindFont("", font) && std::strcmp(font.family, "chainlink_semi_bold") == 0 &&
           view.FindFont("chainlink_semi_bold", font) && std::strcmp(font.family, "chainlink_semi_bold") == 0 &&
           !view.FindFont("ARIAL_NARROW", font) && UnchangedQueries(view);
}

static bool Rejected(const Fixture & invalid, const char * message)
{
    const Fixture original = MakeFixture();
    CatalogView view;
    return Check(view.Init(original.bytes.data(), original.bytes.size()) &&
                 !view.Init(invalid.bytes.data(), invalid.bytes.size()) && BaselineQueries(view),
                 message);
}

static bool ValidCatalogs()
{
    CatalogView view;
    if (!Check(view.ImageCount() == 0 && view.FontCount() == 0 && view.MaterialCount() == 0 && UnchangedQueries(view),
               "empty view queries leave outputs unchanged"))
    {
        return false;
    }

    const Fixture fixture = MakeFixture();
    if (!Check(view.Init(fixture.bytes.data(), fixture.bytes.size()) && BaselineQueries(view),
               "independent synthetic UICT fixture, exact aliases, default short circuit and legacy layout"))
    {
        return false;
    }

    const Fixture maximumImages = MakeFixture(ps2::ui::retail::kMaxImages);
    return Check(view.Init(maximumImages.bytes.data(), maximumImages.bytes.size()) &&
                 view.ImageCount() == ps2::ui::retail::kMaxImages,
                 "maximum image count with contiguous descriptors");
}

static bool InvalidHeaders()
{
    const Fixture valid = MakeFixture();
    CatalogView view;
    if (!Check(!view.Init(nullptr, 0) && !view.Init(nullptr, valid.bytes.size()) &&
               !view.Init(valid.bytes.data(), ps2::ui::retail::kMaxCatalogBytes + 1U),
               "null and excessive buffers"))
    {
        return false;
    }

    for (std::size_t length = 0; length < valid.bytes.size(); ++length)
    {
        Fixture truncated = valid;
        if (length >= kHeaderBytes)
        {
            Put(truncated.bytes, 8, static_cast<std::uint32_t>(length));
        }

        if (!Check(!view.Init(truncated.bytes.data(), length), "every truncation with matching declared length"))
        {
            return false;
        }
    }

    struct Change
    {
        std::uint32_t offset;
        std::uint32_t value;
    };

    static constexpr Change kChanges[] = {
        { 0, 0 }, { 4, 0 }, { 4, 1 }, { 8, 64 }, { 12, 0 }, { 12, 3 }, { 16, 0 }, { 16, 33 }, { 20, 0 }, { 20, 9 }, { 24, 0 }, { 24, 129 }, { 28, 65 }, { 32, 63 }, { 36, 0xFFFFFFFF }, { 40, 0 }, { 44, 0xFFFFFFFF }, { 48, 0 }, { 52, 0 }, { 52, 0xFFFFFFFF }, { 56, 0 }, { 56, 65537 }, { 60, 1 }
    };
    for (const Change & change : kChanges)
    {
        Fixture invalid = valid;
        Put(invalid.bytes, change.offset, change.value);
        if (!Rejected(invalid, "header magic/version/counts/flags/offsets/pool lengths/reserved"))
        {
            return false;
        }
    }

    CatalogView view16;
    const Fixture sixteen = MakeFixture(16, true);
    return Check(view16.Init(sixteen.bytes.data(), sixteen.bytes.size()), "256 GSTX pages across sixteen images") &&
           Rejected(MakeFixture(17, true), "aggregate GSTX page count exceeds 256 within descriptor bound");
}

static bool InvalidRecords()
{
    const Fixture valid = MakeFixture();
    const std::uint32_t descriptor = Word(valid.bytes, kHeaderBytes + 20U);

    struct Change
    {
        std::uint32_t offset;
        std::uint32_t value;
    };

    const Change changes[] = {
        { kHeaderBytes, 1 }, { kHeaderBytes + 4U, 0xFFFFFFFF }, { kHeaderBytes + 8U, 0 }, { kHeaderBytes + 8U, 129 }, { kHeaderBytes + 12U, 16385 }, { kHeaderBytes + 16U, 0 }, { kHeaderBytes + 20U, descriptor + 1U }, { kHeaderBytes + 24U, 32 }, { kHeaderBytes + 24U, 0xFFFFFFFF }, { kHeaderBytes + 28U, 1 }, { descriptor, 0 }, { descriptor + 4U, 2 }, { descriptor + 8U, 0 }, { descriptor + 12U, 0 }, { descriptor + 20U, 0 }, { descriptor + 24U, 1 }, { descriptor + 28U, 1 }, { descriptor + 48U, 0 }, { descriptor + 56U, 0 }, { descriptor + 80U, 0 }, { descriptor + 100U, 0 }, { valid.fonts + 12U, 25 }, { valid.fonts + 12U, 27 }, { valid.fonts + 12U, static_cast<std::uint32_t>(ps2::ui::fonts::kMaxFontBytes + 14U) }, { valid.fonts + 20U, 2 }, { valid.fonts + 24U, 0 }, { valid.fonts + 28U, 4097 }, { valid.fonts + 48U, 1 }, { valid.fonts + 52U, 1 }, { valid.aliases + 4U, 2 }, { valid.materials + 4U, 2 }
    };
    for (const Change & change : changes)
    {
        Fixture invalid = valid;
        Put(invalid.bytes, change.offset, change.value);
        if (!Rejected(invalid, "image/font/alias/material reference, bounds, descriptor and metric validation"))
        {
            return false;
        }
    }

    Fixture duplicate = valid;
    Put(duplicate.bytes, kHeaderBytes + kImageBytes, Word(valid.bytes, kHeaderBytes));
    if (!Rejected(duplicate, "duplicate image identities"))
    {
        return false;
    }

    duplicate = valid;
    Put(duplicate.bytes, valid.fonts + kFontBytes, Word(valid.bytes, valid.fonts));
    Put(duplicate.bytes, valid.fonts + kFontBytes + 4U, Word(valid.bytes, valid.fonts + 4U));
    if (!Rejected(duplicate, "duplicate font families"))
    {
        return false;
    }

    duplicate = valid;
    Put(duplicate.bytes, valid.aliases + 8U, Word(valid.bytes, valid.aliases));
    if (!Rejected(duplicate, "duplicate alias identity"))
    {
        return false;
    }

    duplicate = valid;
    Put(duplicate.bytes, valid.materials + 8U, Word(valid.bytes, valid.materials));
    if (!Rejected(duplicate, "duplicate material identity"))
    {
        return false;
    }

    Fixture missingCanonical = valid;
    Put(missingCanonical.bytes, valid.aliases + 8U, AppendString(missingCanonical, "different_name"));
    if (!Rejected(missingCanonical, "missing canonical self alias cannot break recursive native loading"))
    {
        return false;
    }

    Fixture wrongCanonical = valid;
    Put(wrongCanonical.bytes, valid.aliases + 8U + 4U, 1);
    return Rejected(wrongCanonical, "canonical family alias must select its own font record");
}

static bool InvalidStrings()
{
    const Fixture valid = MakeFixture();
    static constexpr const char * kInvalidPaths[] = {
        "", "/absolute", "../escape", "dir/../escape", "dir/./escape", "dir//escape", "dir/", "host:path", "dir\\escape"
    };
    for (const char * path : kInvalidPaths)
    {
        Fixture invalid = valid;
        Put(invalid.bytes, kHeaderBytes, AppendString(invalid, path));
        if (!Rejected(invalid, "unsafe logical image path"))
        {
            return false;
        }
    }

    struct Change
    {
        std::uint32_t offset;
        const char * text;
    };

    const Change changes[] = {
        { kHeaderBytes + 4U, "assets/not-a-content-hash.uip" }, { valid.fonts, "Arial_Narrow" }, { valid.fonts, "arial/narrow" }, { valid.fonts + 4U, "newfonts/wrong/48.dat" }, { valid.fonts + 8U, "assets/../font.dat" }, { valid.aliases, "font/name" }, { valid.materials, "../material" }
    };
    for (const Change & change : changes)
    {
        Fixture invalid = valid;
        Put(invalid.bytes, change.offset, AppendString(invalid, change.text));
        if (!Rejected(invalid, "family/font path/asset hash/alias/material identity grammar"))
        {
            return false;
        }
    }

    Fixture invalid = valid;
    Put(invalid.bytes, kHeaderBytes, Word(valid.bytes, 56));
    if (!Rejected(invalid, "string offset at the exclusive end"))
    {
        return false;
    }

    invalid = valid;
    invalid.bytes.back() = 'x';
    if (!Rejected(invalid, "unterminated string pool"))
    {
        return false;
    }

    invalid = valid;
    invalid.bytes[valid.strings] = 0x80;
    if (!Rejected(invalid, "non-ASCII string data"))
    {
        return false;
    }

    invalid = valid;
    Put(invalid.bytes, kHeaderBytes + 4U, AppendString(invalid, "assets/" + std::string(64, 'g') + ".uip"));
    if (!Rejected(invalid, "content-addressed asset hash permits only lowercase hexadecimal"))
    {
        return false;
    }

    invalid = valid;
    Put(invalid.bytes, valid.fonts, AppendString(invalid, std::string(64, 'a')));
    if (!Rejected(invalid, "font family exceeds 63 bytes"))
    {
        return false;
    }

    invalid = valid;
    Put(invalid.bytes, valid.aliases, AppendString(invalid, std::string(128, 'a')));
    if (!Rejected(invalid, "font alias exceeds 127 bytes"))
    {
        return false;
    }

    invalid = valid;
    Put(invalid.bytes, kHeaderBytes, AppendString(invalid, std::string(256, 'a')));
    return Rejected(invalid, "string run exceeds 255 bytes");
}

static bool ExerciseView(const CatalogView & view)
{
    for (std::uint32_t index = 0; index < view.ImageCount(); ++index)
    {
        ImageRecord image{};
        if (!view.GetImage(index, image) || image.name == nullptr || image.path == nullptr ||
            image.descriptor == nullptr || image.descriptorBytes < 40)
        {
            return false;
        }

        ImageRecord found{};
        if (!view.FindImage(image.name, found) || found.descriptor != image.descriptor)
        {
            return false;
        }
    }

    for (std::uint32_t index = 0; index < view.FontCount(); ++index)
    {
        FontRecord font{};
        FontRecord found{};
        if (!view.GetFont(index, font) || font.family == nullptr || !view.FindFont(font.family, found) ||
            found.family != font.family || font.imageIndex >= view.ImageCount())
        {
            return false;
        }
    }

    return UnchangedQueries(view);
}

static bool MutationSafety()
{
    const Fixture valid = MakeFixture();
    std::uint32_t random = 0x7019B123;
    for (std::uint32_t run = 0; run < 10000; ++run)
    {
        Fixture mutated = valid;
        for (std::uint32_t change = 0; change <= run % 4U; ++change)
        {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            const auto index = static_cast<std::size_t>(random) % mutated.bytes.size();
            mutated.bytes[index] ^= static_cast<std::uint8_t>(1U << (run % 8U));
        }

        CatalogView view;
        if (view.Init(mutated.bytes.data(), mutated.bytes.size()) &&
            !Check(ExerciseView(view), "accepted deterministic mutations retain safe coherent queries"))
        {
            return false;
        }
    }

    return true;
}

static bool ReadCatalog(const char * path)
{
    FILE * const file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return Check(false, "optional local catalog missing");
    }

    const bool seek = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seek ? std::ftell(file) : -1;
    if (length <= 0 || static_cast<unsigned long>(length) > ps2::ui::retail::kMaxCatalogBytes ||
        std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return Check(false, "optional local catalog length");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    CatalogView view;
    return Check(read && closed && view.Init(bytes.data(), bytes.size()) && ExerciseView(view),
                 "optional local catalog admission and canonical bindings");
}

} // namespace

int main(int argc, char ** argv)
{
    if (!ValidCatalogs() || !InvalidHeaders() || !InvalidRecords() || !InvalidStrings() || !MutationSafety())
    {
        return 1;
    }

    for (int index = 1; index < argc; ++index)
    {
        if (!ReadCatalog(argv[index]))
        {
            return 1;
        }
    }

    std::puts("UI retail catalog tests passed");
    return 0;
}
