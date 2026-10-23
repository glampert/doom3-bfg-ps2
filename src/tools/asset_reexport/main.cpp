// ================================================================================================
// File: main.cpp
// Brief: Re-export retail Doom 3 BFG textures in PS2-native form. The `ui` command converts the
// interface images a job file lists into paletted GSTX textures, with a report and previews.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bimage.h"
#include "assets/glyph_set.h"
#include "assets/json.h"
#include "assets/png.h"
#include "assets/resources.h"
#include "assets/texture_convert.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace assets;

static constexpr std::size_t kMaxJobBytes = 16U * 1024U * 1024U;
static constexpr std::size_t kMaxTextures = 256;
static constexpr std::size_t kMaxGlyphs = 1024;
static constexpr std::size_t kMaxGlyphSizes = 16;

struct TextureJob
{
    std::string name;
    std::string source;
    std::string output;
    int width = 0;
    int height = 0;
    ConvertPolicy policy;
    std::vector<PlacementRequest> placements;
    // A glyph set renders the source font atlas's glyphs at these sizes instead of converting it.
    bool glyphSet = false;
    std::vector<GlyphMetrics> glyphs;
    std::vector<GlyphSizeRequest> glyphSizes;
};

static bool Fail(const std::string & message)
{
    std::cerr << "asset_reexport: " << message << '\n';
    return false;
}

static bool ReadFile(const fs::path & path, std::size_t limit, std::string & output)
{
    std::ifstream stream(path, std::ios::binary);

    if (!stream)
    {
        return Fail("cannot open " + path.string());
    }

    std::ostringstream text;
    text << stream.rdbuf();
    output = text.str();
    return output.size() <= limit || Fail("job file too large: " + path.string());
}

static bool WriteFile(const fs::path & path, const void * data, std::size_t bytes)
{
    std::FILE * stream = std::fopen(path.string().c_str(), "wb");

    if (stream == nullptr)
    {
        return Fail("cannot create " + path.string());
    }

    const bool written = bytes == 0 || std::fwrite(data, 1, bytes, stream) == bytes;
    return (std::fclose(stream) == 0 && written) || Fail("cannot write " + path.string());
}

// A plain file name: the job may not choose directories or escape the output root.
static bool SafeFileName(const std::string & name)
{
    if (name.empty() || name.size() > 128 || name.front() == '.')
    {
        return false;
    }

    for (const char character : name)
    {
        const bool valid = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';

        if (!valid)
        {
            return false;
        }
    }

    return true;
}

static bool GetRect(const JsonValue * value, Rect & output)
{
    if (value == nullptr || value->type != JsonValue::Type::Array || value->items.size() != 4)
    {
        return false;
    }

    int fields[4] = {};

    for (std::size_t index = 0; index < 4; ++index)
    {
        if (!value->items[index].IsInteger(0, kGstxMaxLogicalExtent))
        {
            return false;
        }

        fields[index] = static_cast<int>(value->items[index].number);
    }

    output = { fields[0], fields[1], fields[2], fields[3] };
    return true;
}

static bool GetNumber(const JsonValue & object, const char * key, double minimum, double maximum, double & output)
{
    const JsonValue * value = object.Find(key);

    if (value == nullptr)
    {
        return true;
    }

    if (value->type != JsonValue::Type::Number || value->number < minimum || value->number > maximum)
    {
        return Fail(std::string("policy field out of range: ") + key);
    }

    output = value->number;
    return true;
}

static bool GetInteger(const JsonValue & object, const char * key, int minimum, int maximum, int & output)
{
    const JsonValue * value = object.Find(key);

    if (value == nullptr)
    {
        return true;
    }

    if (!value->IsInteger(minimum, maximum))
    {
        return Fail(std::string("policy field out of range or not an integer: ") + key);
    }

    output = static_cast<int>(value->number);
    return true;
}

// Policy fields override the defaults they are applied to; unknown keys are refused.
static bool ApplyPolicy(const JsonValue & object, ConvertPolicy & policy)
{
    static const char * const kKeys[] = { "scale", "max_page_extent", "t4_max_rmse", "t4_max_error", "share_palettes",
                                          "group8_max_rmse", "group8_max_increase", "group4_max_rmse", "group4_max_increase",
                                          "group_max_error", "alpha_levels" };

    if (object.type != JsonValue::Type::Object)
    {
        return Fail("policy must be an object");
    }

    for (const std::string & key : object.keys)
    {
        if (std::find(std::begin(kKeys), std::end(kKeys), key) == std::end(kKeys))
        {
            return Fail("unknown policy field: " + key);
        }
    }

    int groupMaxError = policy.group8.maxError;

    if (!GetNumber(object, "scale", 0.01, 1.0, policy.scale) ||
        !GetInteger(object, "max_page_extent", 128, kGstxMaxPageExtent, policy.maxPageExtent) ||
        !GetNumber(object, "t4_max_rmse", 0.0, 255.0, policy.t4MaxRmse) || !GetInteger(object, "t4_max_error", 0, 255, policy.t4MaxError) ||
        !GetNumber(object, "group8_max_rmse", 0.0, 255.0, policy.group8.maxRmse) ||
        !GetNumber(object, "group8_max_increase", 0.0, 255.0, policy.group8.maxIncrease) ||
        !GetNumber(object, "group4_max_rmse", 0.0, 255.0, policy.group4.maxRmse) ||
        !GetNumber(object, "group4_max_increase", 0.0, 255.0, policy.group4.maxIncrease) ||
        !GetInteger(object, "group_max_error", 0, 255, groupMaxError) || !GetInteger(object, "alpha_levels", 2, 16, policy.alphaLevels))
    {
        return false;
    }

    policy.group8.maxError = groupMaxError;
    policy.group4.maxError = groupMaxError;

    if (policy.maxPageExtent % 128 != 0)
    {
        return Fail("max_page_extent must be a multiple of 128");
    }

    if (const JsonValue * share = object.Find("share_palettes"))
    {
        if (share->type != JsonValue::Type::Boolean)
        {
            return Fail("share_palettes must be a boolean");
        }

        policy.sharePalettes = share->boolean;
    }

    return true;
}

// "glyphs": {"glyphs": [{"codepoint", "s", "t", "width", "height", "left", "top"}, ...],
//            "sizes": [{"scale": [x, y], "stroke_weight": w}, ...]}, glyphs in codepoint order.
static bool ParseGlyphSet(const JsonValue & object, TextureJob & texture)
{
    const JsonValue * glyphs = object.Find("glyphs");
    const JsonValue * sizes = object.Find("sizes");

    if (object.type != JsonValue::Type::Object || glyphs == nullptr || glyphs->type != JsonValue::Type::Array ||
        glyphs->items.empty() || glyphs->items.size() > kMaxGlyphs || sizes == nullptr || sizes->type != JsonValue::Type::Array ||
        sizes->items.empty() || sizes->items.size() > kMaxGlyphSizes)
    {
        return Fail("glyph set needs 1-1024 glyphs and 1-16 sizes: " + texture.name);
    }

    for (const JsonValue & item : glyphs->items)
    {
        static const char * const kFields[] = { "codepoint", "s", "t", "width", "height", "left", "top" };
        int values[7] = {};

        for (std::size_t field = 0; field < 7; ++field)
        {
            const JsonValue * value = item.Find(kFields[field]);
            const int minimum = field == 0 ? 0 : (field >= 5 ? -4096 : 0);
            const int maximum = field == 0 ? 0x10FFFF : 4096;

            if (item.type != JsonValue::Type::Object || value == nullptr || !value->IsInteger(minimum, maximum))
            {
                return Fail(std::string("glyph field missing or out of range: ") + kFields[field] + " in " + texture.name);
            }

            values[field] = static_cast<int>(value->number);
        }

        texture.glyphs.push_back({ static_cast<std::uint32_t>(values[0]), values[1], values[2], values[3], values[4], values[5], values[6] });
    }

    for (const JsonValue & item : sizes->items)
    {
        const JsonValue * scale = item.Find("scale");
        GlyphSizeRequest request;

        if (item.type != JsonValue::Type::Object || scale == nullptr || scale->type != JsonValue::Type::Array || scale->items.size() != 2 ||
            scale->items[0].type != JsonValue::Type::Number || scale->items[1].type != JsonValue::Type::Number ||
            !GetNumber(item, "stroke_weight", 0.0, 16.0, request.strokeWeight))
        {
            return Fail("glyph size needs a scale pair and a stroke weight: " + texture.name);
        }

        request.scaleX = scale->items[0].number;
        request.scaleY = scale->items[1].number;
        texture.glyphSizes.push_back(request);
    }

    texture.glyphSet = true;
    return true;
}

static bool ParseJob(const JsonValue & root, std::vector<std::string> & containers, std::vector<TextureJob> & textures)
{
    const JsonValue * version = root.Find("version");

    if (root.type != JsonValue::Type::Object || version == nullptr || !version->IsInteger(1, 1))
    {
        return Fail("job must be an object with version 1");
    }

    containers = { "_common.resources", "_ordered.resources" };

    if (const JsonValue * list = root.Find("containers"))
    {
        containers.clear();

        if (list->type != JsonValue::Type::Array || list->items.empty())
        {
            return Fail("containers must be a non-empty array");
        }

        for (const JsonValue & item : list->items)
        {
            if (item.type != JsonValue::Type::String || !SafeFileName(item.text))
            {
                return Fail("container names must be plain file names");
            }

            containers.push_back(item.text);
        }
    }

    ConvertPolicy defaults;

    if (const JsonValue * policy = root.Find("policy"); policy != nullptr && !ApplyPolicy(*policy, defaults))
    {
        return false;
    }

    const JsonValue * list = root.Find("textures");

    if (list == nullptr || list->type != JsonValue::Type::Array || list->items.empty() || list->items.size() > kMaxTextures)
    {
        return Fail("textures must be a non-empty array of at most 256 entries");
    }

    for (const JsonValue & item : list->items)
    {
        const JsonValue * name = item.Find("name");
        const JsonValue * source = item.Find("source");
        const JsonValue * output = item.Find("output");

        if (item.type != JsonValue::Type::Object || name == nullptr || name->type != JsonValue::Type::String ||
            source == nullptr || source->type != JsonValue::Type::String || output == nullptr ||
            output->type != JsonValue::Type::String || !SafeFileName(output->text))
        {
            return Fail("each texture needs a name, a source resource and a plain output file name");
        }

        TextureJob texture;
        texture.name = name->text;
        texture.source = source->text;
        texture.output = output->text;
        texture.policy = defaults;

        for (const TextureJob & previous : textures)
        {
            if (previous.output == texture.output)
            {
                return Fail("duplicate texture output: " + texture.output);
            }
        }

        if (const JsonValue * policy = item.Find("policy"); policy != nullptr && !ApplyPolicy(*policy, texture.policy))
        {
            return false;
        }

        const JsonValue * width = item.Find("width");
        const JsonValue * height = item.Find("height");

        if ((width != nullptr && !width->IsInteger(1, kGstxMaxLogicalExtent)) || (height != nullptr && !height->IsInteger(1, kGstxMaxLogicalExtent)))
        {
            return Fail("texture dimensions out of range: " + texture.name);
        }

        texture.width = width != nullptr ? static_cast<int>(width->number) : 0;
        texture.height = height != nullptr ? static_cast<int>(height->number) : 0;

        if (const JsonValue * placements = item.Find("placements"))
        {
            if (placements->type != JsonValue::Type::Array || placements->items.size() > static_cast<std::size_t>(kGstxMaxPlacements))
            {
                return Fail("placements must be an array of at most 1024 entries: " + texture.name);
            }

            for (const JsonValue & entry : placements->items)
            {
                PlacementRequest request;

                if (entry.type != JsonValue::Type::Object || !GetRect(entry.Find("match"), request.match) ||
                    !GetRect(entry.Find("source"), request.source))
                {
                    return Fail("placement needs match and source rectangles: " + texture.name);
                }

                if (const JsonValue * scale = entry.Find("scale"))
                {
                    if (scale->type != JsonValue::Type::Number || scale->number <= 0.0 || scale->number > 1.0)
                    {
                        return Fail("placement scale out of range: " + texture.name);
                    }

                    request.scale = scale->number;
                }

                texture.placements.push_back(request);
            }
        }

        if (const JsonValue * glyphs = item.Find("glyphs"))
        {
            if (!texture.placements.empty() || texture.width != 0 || texture.height != 0 || !ParseGlyphSet(*glyphs, texture))
            {
                return Fail("a glyph set takes no placements or dimensions: " + texture.name);
            }
        }

        textures.push_back(std::move(texture));
    }

    return true;
}

static std::string RectJson(const Rect & rect)
{
    return "[" + std::to_string(rect.x) + ", " + std::to_string(rect.y) + ", " + std::to_string(rect.width) + ", " +
           std::to_string(rect.height) + "]";
}

static std::string Number(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.4f", value);
    return text;
}

static const char * FormatName(IndexFormat format)
{
    return format == IndexFormat::T8 ? "psmt8" : "psmt4";
}

// The source as the converted texture covers it: match rectangles only, transparent elsewhere.
static RgbaImage MatchedSource(const RgbaImage & image, const GstxTexture & texture)
{
    RgbaImage output;
    output.Allocate(image.width, image.height);

    for (const GstxPlacement & placement : texture.placements)
    {
        for (int y = placement.match.y; y < placement.match.y + placement.match.height; ++y)
        {
            for (int x = placement.match.x; x < placement.match.x + placement.match.width; ++x)
            {
                std::copy(image.At(x, y), image.At(x, y) + 4, output.At(x, y));
            }
        }
    }

    return output;
}

static bool ConvertUi(const fs::path & data, const fs::path & jobPath, const fs::path & output, const fs::path & previews)
{
    std::string text;
    JsonValue root;
    std::string error;
    std::vector<std::string> containerNames;
    std::vector<TextureJob> textures;

    if (!ReadFile(jobPath, kMaxJobBytes, text) || (!ParseJson(text, root, error) && Fail("job: " + error)) ||
        !ParseJob(root, containerNames, textures))
    {
        return false;
    }

    std::vector<fs::path> containers;

    for (const std::string & name : containerNames)
    {
        containers.push_back(data / name);
    }

    ResourceCatalog catalog;

    if (!catalog.Open(containers, error))
    {
        return Fail(error);
    }

    std::error_code status;
    fs::create_directories(output, status);

    if (status || (!previews.empty() && (fs::create_directories(previews, status), status)))
    {
        return Fail("cannot create output directories");
    }

    std::string report = "{\n  \"version\": 1,\n  \"textures\": [";
    std::size_t totalFile = 0;
    std::size_t totalDecoded = 0;
    std::size_t totalVram = 0;

    for (std::size_t index = 0; index < textures.size(); ++index)
    {
        const TextureJob & job = textures[index];
        const auto started = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> bytes;
        RgbaImage image;
        ConvertResult result;
        std::vector<std::uint8_t> file;
        GstxSizes sizes;
        GstxTexture check;

        if (!catalog.Read(job.source, bytes, error) || !DecodeBimage(bytes, 0, image, error))
        {
            return Fail(job.name + ": " + error);
        }

        if ((job.width != 0 && job.width != image.width) || (job.height != 0 && job.height != image.height))
        {
            return Fail(job.name + ": source dimensions differ from the job");
        }

        GlyphSetResult glyphSet;

        if (job.glyphSet)
        {
            if (!RenderGlyphSet(image, job.glyphs, job.glyphSizes, job.policy.maxPageExtent, glyphSet, error))
            {
                return Fail(job.name + ": " + error);
            }

            result.texture = glyphSet.texture;
            result.placements = glyphSet.placements;
            result.alphaOnly = true;
        }
        else if (!ConvertTexture(image, job.placements, job.policy, result, error))
        {
            return Fail(job.name + ": " + error);
        }

        if (!WriteGstx(result.texture, file, sizes, error) || !ReadGstx(file, check, error))
        {
            return Fail(job.name + ": " + error);
        }

        if (!WriteFile(output / job.output, file.data(), file.size()))
        {
            return false;
        }

        if (!previews.empty())
        {
            const std::string stem = job.output.substr(0, job.output.rfind('.'));

            // A glyph set's logical space is its sheet of pre-rendered bitmaps, not the source atlas.
            if ((!job.glyphSet && !WritePng(previews / (stem + ".original.png"), MatchedSource(image, check), error)) ||
                !WritePng(previews / (stem + ".converted.png"), RenderGstx(check), error))
            {
                return Fail(error);
            }
        }

        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        int t4 = 0;
        int t8 = 0;
        report += index == 0 ? "\n" : ",\n";
        report += "    {\n      \"name\": " + JsonString(job.name) + ",\n      \"source\": " + JsonString(job.source) +
                  ",\n      \"output\": " + JsonString(job.output) + ",\n      \"logical\": [" + std::to_string(check.logicalWidth) + ", " +
                  std::to_string(check.logicalHeight) + "],\n      \"source_bytes\": " + std::to_string(bytes.size()) +
                  ",\n      \"file_bytes\": " + std::to_string(sizes.fileBytes) + ",\n      \"table_bytes\": " +
                  std::to_string(sizes.tableBytes) + ",\n      \"decoded_bytes\": " + std::to_string(sizes.decodedBytes) +
                  ",\n      \"vram_bytes\": " + std::to_string(sizes.vramBytes) + ",\n      \"alpha_only\": " +
                  (result.alphaOnly ? "true" : "false") + ",\n      \"seconds\": " + Number(seconds) + ",\n      \"pages\": [";

        for (std::size_t page = 0; page < check.pages.size(); ++page)
        {
            report += (page == 0 ? "" : ", ") + std::string("{\"width\": ") + std::to_string(check.pages[page].width) +
                      ", \"height\": " + std::to_string(check.pages[page].height) + ", \"format\": \"" +
                      FormatName(check.pages[page].format) + "\"}";
        }

        report += "],\n      \"cluts\": [";

        for (std::size_t clut = 0; clut < check.cluts.size(); ++clut)
        {
            report += (clut == 0 ? "" : ", ") + std::to_string(check.cluts[clut].texels.size());
        }

        report += "],\n      \"placements\": [";

        for (std::size_t placement = 0; placement < result.placements.size(); ++placement)
        {
            const PlacementResult & entry = result.placements[placement];
            (entry.format == IndexFormat::T4 ? t4 : t8) += 1;
            report += (placement == 0 ? "\n" : ",\n") + std::string("        {\"match\": ") + RectJson(entry.placement.match) +
                      ", \"source\": " + RectJson(entry.placement.source) + ", \"dest\": " + RectJson(entry.placement.dest) +
                      ", \"page\": " + std::to_string(entry.placement.page) + ", \"clut\": " + std::to_string(entry.placement.clut) +
                      ", \"format\": \"" + FormatName(entry.format) + "\", \"rmse\": " + Number(entry.quality.rmse) +
                      ", \"max_error\": " + std::to_string(entry.quality.maxError) + ", \"over8\": " + Number(entry.quality.over8) + "}";
        }

        report += "\n      ],\n      \"psmt4_placements\": " + std::to_string(t4) + ",\n      \"psmt8_placements\": " + std::to_string(t8);

        if (job.glyphSet)
        {
            report += ",\n      \"glyph_set\": [";

            for (std::size_t size = 0; size < glyphSet.sizes.size(); ++size)
            {
                const GlyphSizeResult & entry = glyphSet.sizes[size];
                report += (size == 0 ? "\n" : ",\n") + std::string("        {\"stroke_weight\": ") + Number(entry.request.strokeWeight) +
                          ", \"glyphs\": [";

                for (std::size_t glyph = 0; glyph < entry.glyphs.size(); ++glyph)
                {
                    const PrerenderedGlyphResult & item = entry.glyphs[glyph];
                    report += (glyph == 0 ? "" : ", ") + std::string("{\"codepoint\": ") + std::to_string(item.codepoint) +
                              ", \"rect\": " + (item.rect.width == 0 ? std::string("null") : RectJson(item.rect)) +
                              ", \"outline\": " + (item.outline ? "true" : "false") + ", \"origin\": [" + std::to_string(item.originX) +
                              ", " + std::to_string(item.originY) + "]}";
                }

                report += "]}";
            }

            report += "\n      ]";
        }

        report += "\n    }";
        totalFile += sizes.fileBytes;
        totalDecoded += sizes.decodedBytes;
        totalVram += sizes.vramBytes;
        std::cout << job.name << ": " << image.width << "x" << image.height << " -> " << check.pages.size() << " page(s), "
                  << check.cluts.size() << " CLUT(s), " << t8 << " psmt8 / " << t4 << " psmt4 placement(s), " << sizes.fileBytes
                  << " bytes on disc, " << sizes.decodedBytes << " in memory\n";
    }

    report += "\n  ],\n  \"file_bytes\": " + std::to_string(totalFile) + ",\n  \"decoded_bytes\": " + std::to_string(totalDecoded) +
              ",\n  \"vram_bytes\": " + std::to_string(totalVram) + "\n}\n";
    return WriteFile(output / "report.json", report.data(), report.size());
}

static void Usage()
{
    std::cout << "Usage: asset_reexport ui --data DIR --job FILE.json --output DIR [--previews DIR]\n"
                 "Converts the retail interface textures a job lists into paletted GSTX files.\n"
                 "--data names the retail installation holding the job's .resources containers.\n"
                 "--output receives one file per texture and report.json; existing files are replaced.\n"
                 "--previews also writes <output>.original.png and <output>.converted.png per texture.\n";
}

static int Run(int argc, char ** argv)
{
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")
    {
        Usage();
        return argc < 2 ? 2 : 0;
    }

    if (std::string(argv[1]) != "ui")
    {
        Fail("unknown command: " + std::string(argv[1]));
        return 2;
    }

    fs::path data;
    fs::path job;
    fs::path output;
    fs::path previews;

    for (int index = 2; index < argc; ++index)
    {
        const std::string argument(argv[index]);
        fs::path * target = argument == "--data"       ? &data
                            : argument == "--job"      ? &job
                            : argument == "--output"   ? &output
                            : argument == "--previews" ? &previews
                                                       : nullptr;

        if (target == nullptr || index + 1 >= argc || !target->empty())
        {
            Fail("unknown, repeated or incomplete option: " + argument);
            return 2;
        }

        *target = argv[++index];
    }

    if (data.empty() || job.empty() || output.empty())
    {
        Usage();
        return 2;
    }

    return ConvertUi(data, job, output, previews) ? 0 : 1;
}

} // namespace

int main(int argc, char ** argv)
{
    const int result = Run(argc, argv);
    std::cout.flush();
    return result;
}
