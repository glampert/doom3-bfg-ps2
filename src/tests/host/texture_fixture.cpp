// ================================================================================================
// File: texture_fixture.cpp
// Brief: Write the GSTX v2 files the renderer gate uploads and draws: a PSMT8 and a PSMT4 texture
// whose every texel and palette entry follows a formula, so the runner checks each captured pixel
// against the formula rather than against the cook that laid the files out.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The formulas are repeated in src/tools/scripts/renderer_capture.py (texture_fixture_*); keep the
// two in step.

#include "assets/gstx_v2.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using namespace assets;

static std::uint32_t Rgba(std::uint32_t r, std::uint32_t g, std::uint32_t b)
{
    return (r & 0xFFU) | ((g & 0xFFU) << 8) | ((b & 0xFFU) << 16) | (0x80U << 24);
}

// 64x64 PSMT8, four levels, repeating.
static GstxV2Image Index8()
{
    GstxV2Image image;
    image.width = 64;
    image.height = 64;
    image.psm = GsPsm::T8;

    for (int level = 0; level < 4; ++level)
    {
        const int width = 64 >> level;
        const int height = 64 >> level;
        std::vector<std::uint32_t> texels;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                texels.push_back(static_cast<std::uint32_t>(x * 5 + y * 3 + level * 61) & 0xFFU);
            }
        }

        image.levels.push_back(std::move(texels));
    }

    for (std::uint32_t entry = 0; entry < 256; ++entry)
    {
        image.palette.push_back(Rgba(entry, entry * 3U, 255U - entry));
    }

    return image;
}

// 64x32 PSMT4, three levels, clamped.
static GstxV2Image Index4()
{
    GstxV2Image image;
    image.width = 64;
    image.height = 32;
    image.psm = GsPsm::T4;
    image.wrapS = GstxWrap::Clamp;
    image.wrapT = GstxWrap::Clamp;

    for (int level = 0; level < 3; ++level)
    {
        const int width = 64 >> level;
        const int height = 32 >> level;
        std::vector<std::uint32_t> texels;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                texels.push_back(static_cast<std::uint32_t>(x / 3 + y * 7 + level * 5) & 0xFU);
            }
        }

        image.levels.push_back(std::move(texels));
    }

    for (std::uint32_t entry = 0; entry < 16; ++entry)
    {
        image.palette.push_back(Rgba(entry * 16U + 8U, 255U - entry * 16U, entry * 85U));
    }

    return image;
}

static bool Write(const std::string & path, const GstxV2Image & image)
{
    std::vector<std::uint8_t> file;
    std::string error;

    if (!WriteGstxV2(image, file, error))
    {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return false;
    }

    FILE * const output = std::fopen(path.c_str(), "wb");
    const bool written = output != nullptr && std::fwrite(file.data(), 1, file.size(), output) == file.size();

    if (output == nullptr || std::fclose(output) != 0 || !written)
    {
        std::fprintf(stderr, "%s: write failed\n", path.c_str());
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char ** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: texture_fixture OUTPUT_DIRECTORY\n");
        return 2;
    }

    const std::string directory(argv[1]);
    return Write(directory + "/renderer-t8.gstx", Index8()) && Write(directory + "/renderer-t4.gstx", Index4()) ? 0 : 1;
}
