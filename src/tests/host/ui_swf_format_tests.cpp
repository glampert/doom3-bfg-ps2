// ================================================================================================
// File: ui_swf_format_tests.cpp
// Brief: Exercise storage-only BSWF admission under sanitizers before any native allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/swf_format.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
using Bytes = std::vector<unsigned char>;

Bytes Read(const char * path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool RejectName(const char *, bool, void *)
{
    return false;
}

void Reject(const Bytes & bytes)
{
    ps2::ui::SwfSummary summary;
    summary.dictionary = 0x12345678;
    assert(!ps2::ui::ValidateSwfBytes(bytes.data(), bytes.size(), summary));
    assert(summary.dictionary == 0x12345678);
}

void Check(const Bytes & bytes, bool exhaustive)
{
    ps2::ui::SwfSummary summary;
    assert(ps2::ui::ValidateSwfBytes(bytes.data(), bytes.size(), summary));
    assert(summary.dictionary > 0 && summary.editTexts > 0 && summary.fonts > 0);
    assert(!ps2::ui::ValidateSwfBytes(bytes.data(), bytes.size(), summary, RejectName));
    const std::size_t stride = exhaustive ? 1 : bytes.size() / 512 + 1;

    for (std::size_t size = 0; size < bytes.size(); size += stride)
    {
        Reject(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)));
    }

    auto extended = bytes;
    extended.push_back(0);
    Reject(extended);
    auto corrupted = bytes;
    corrupted[0] ^= 1;
    Reject(corrupted);
    corrupted = bytes;
    corrupted[12] = 0x7f;
    corrupted[13] = 0x80;
    Reject(corrupted);
    corrupted = bytes;
    // Main timeline's first count is immediately after its frame count.
    corrupted[24] = 0x80;
    Reject(corrupted);

    unsigned state = 42;
    for (unsigned mutation = 0; mutation < 4000; ++mutation)
    {
        state = state * 1664525U + 1013904223U;
        corrupted = bytes;
        const std::size_t offset = state % bytes.size();
        corrupted[offset] ^= static_cast<unsigned char>((state >> 24) | 1U);
        ps2::ui::SwfSummary ignored;
        (void)ps2::ui::ValidateSwfBytes(corrupted.data(), corrupted.size(), ignored);
    }

    std::printf("BSWF storage validator: bytes=%zu dictionary=%u sprites=%u commands=%u fonts=%u texts=%u preview=%u\n",
                bytes.size(), summary.dictionary, summary.sprites, summary.commands, summary.fonts, summary.editTexts, summary.previewText);
}
} // namespace

int main(int argc, char ** argv)
{
    ps2::ui::SwfSummary summary;
    assert(!ps2::ui::ValidateSwfBytes(nullptr, 0, summary));
    Check(Read("assets/ui/base/generated/swf/ps2_demo.bswf"), true);

    const char language[] = "{\n\"#str_a\" \"Text\\nLine\"\n}\n";
    std::uint32_t entries = 99;
    assert(ps2::ui::ValidateEnglishBytes(language, sizeof(language) - 1, entries) && entries == 1);
    const char badEscape[] = "{\n\"#str_a\" \"Text\\";
    assert(!ps2::ui::ValidateEnglishBytes(badEscape, sizeof(badEscape) - 1, entries));
    assert(entries == 1);
    const char sameLine[] = "{ \"#str_a\" \"A\" \"#str_b\" \"B\" }";
    assert(!ps2::ui::ValidateEnglishBytes(sameLine, sizeof(sameLine) - 1, entries));

    for (int argument = 1; argument < argc; ++argument)
    {
        const Bytes bytes = Read(argv[argument]);

        if (std::strstr(argv[argument], ".lang") != nullptr)
        {
            assert(ps2::ui::ValidateEnglishBytes(bytes.data(), bytes.size(), entries));
            std::printf("English storage validator: bytes=%zu entries=%u\n", bytes.size(), entries);
        }
        else
        {
            Check(bytes, false);
        }
    }

    return 0;
}
