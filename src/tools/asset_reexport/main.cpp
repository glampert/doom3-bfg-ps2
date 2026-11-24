// ================================================================================================
// File: main.cpp
// Brief: Re-export Doom 3 BFG assets in PS2-native form. The `ui` command converts the interface
// images a job file lists into paletted GSTX textures, with a report and previews; the `world`
// command cooks a map's .proc render geometry into a D3WD world file and its report.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bimage.h"
#include "assets/glyph_set.h"
#include "assets/json.h"
#include "assets/light.h"
#include "assets/lightmap.h"
#include "assets/map_entities.h"
#include "assets/material.h"
#include "assets/material_passes.h"
#include "assets/png.h"
#include "assets/resources.h"
#include "assets/static_models.h"
#include "assets/texture_convert.h"
#include "assets/texture_pack.h"
#include "assets/world.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
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
static constexpr std::size_t kMaxGlyphSizes = 32;

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
        return Fail("glyph set needs 1-1024 glyphs and 1-32 sizes: " + texture.name);
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

// ------------------------------------------------------------------------------------------------
// World
// ------------------------------------------------------------------------------------------------

static constexpr std::size_t kMaxProcBytes = 256U * 1024U * 1024U;

static bool GetColor(const JsonValue * value, std::array<float, 3> & output)
{
    if (value == nullptr || value->type != JsonValue::Type::Array || value->items.size() != 3)
    {
        return false;
    }

    for (std::size_t index = 0; index < 3; ++index)
    {
        const JsonValue & item = value->items[index];

        if (item.type != JsonValue::Type::Number || item.number < 0.0 || item.number > 2.0)
        {
            return false;
        }

        output[index] = static_cast<float>(item.number);
    }

    return true;
}

// A relative path below --data: no '..', no leading slash, no backslashes.
static bool RelativeDataPath(const JsonValue * value)
{
    return value != nullptr && value->type == JsonValue::Type::String && !value->text.empty() && value->text.size() <= 128 &&
           value->text.find("..") == std::string::npos && value->text.front() != '/' && value->text.find('\\') == std::string::npos;
}

// The job's optional texture stage: where id's materials are, and the cook policy.
struct WorldTextureJob
{
    bool enabled = false;
    std::vector<std::string> materialContainers;
    TexturePolicy policy;
    // Bump maps baked into the diffuse (off at amplitude 0).
    CavityPolicy cavity;
    // The sky box's faces: one level of this size, a power of two. The sky is magnified on every
    // screen (a face spans more than 90 degrees of a 640-pixel view), so mips would never draw.
    int skyExtent = 128;
};

static bool ParseTextureJob(const JsonValue & value, WorldTextureJob & output)
{
    if (value.type != JsonValue::Type::Object)
    {
        return Fail("textures must be an object");
    }

    for (const std::string & key : value.keys)
    {
        if (key != "material_containers" && key != "scale" && key != "min_extent" && key != "max_extent" && key != "min_mip_extent" &&
            key != "cavity" && key != "cavity_light" && key != "sky_extent")
        {
            return Fail("unknown textures field: " + key);
        }
    }

    const JsonValue * list = value.Find("material_containers");

    if (list == nullptr || list->type != JsonValue::Type::Array || list->items.empty())
    {
        return Fail("textures.material_containers must be a non-empty array");
    }

    for (const JsonValue & item : list->items)
    {
        if (!RelativeDataPath(&item))
        {
            return Fail("textures.material_containers must be relative paths without '..'");
        }

        output.materialContainers.push_back(item.text);
    }

    if (const JsonValue * scale = value.Find("scale"))
    {
        if (scale->type != JsonValue::Type::Number || scale->number <= 0.0 || scale->number > 1.0)
        {
            return Fail("textures.scale must be in (0, 1]");
        }

        output.policy.scale = scale->number;
    }

    if (const JsonValue * extent = value.Find("min_extent"))
    {
        if (!extent->IsInteger(1, kGstxV2MaxExtent))
        {
            return Fail("textures.min_extent must be an integer from 1 to 512");
        }

        output.policy.minExtent = static_cast<int>(extent->number);
    }

    if (const JsonValue * extent = value.Find("max_extent"))
    {
        if (!extent->IsInteger(1, kGstxV2MaxExtent) || static_cast<int>(extent->number) < output.policy.minExtent)
        {
            return Fail("textures.max_extent must be an integer from min_extent to 512");
        }

        output.policy.maxExtent = static_cast<int>(extent->number);
    }

    if (const JsonValue * extent = value.Find("min_mip_extent"))
    {
        if (!extent->IsInteger(1, 512))
        {
            return Fail("textures.min_mip_extent must be an integer from 1 to 512");
        }

        output.policy.minMipExtent = static_cast<int>(extent->number);
    }

    if (const JsonValue * cavity = value.Find("cavity"))
    {
        if (cavity->type != JsonValue::Type::Number || cavity->number < 0.0 || cavity->number > 2.0)
        {
            return Fail("textures.cavity must be a number in [0, 2]");
        }

        output.cavity.amplitude = static_cast<float>(cavity->number);
    }

    if (value.Find("cavity_light") != nullptr &&
        (!GetColor(value.Find("cavity_light"), output.cavity.light) || output.cavity.light[2] <= 0.0f))
    {
        return Fail("textures.cavity_light must be three numbers with z above 0");
    }

    if (const JsonValue * extent = value.Find("sky_extent"))
    {
        const int size = extent->IsInteger(8, 256) ? static_cast<int>(extent->number) : 0;

        if (size == 0 || (size & (size - 1)) != 0)
        {
            return Fail("textures.sky_extent must be a power of two from 8 to 256");
        }

        output.skyExtent = size;
    }

    output.enabled = true;
    return true;
}

struct WorldLightingJob
{
    bool enabled = false;
    // The map script (inside the containers) the light classes read; empty for none.
    std::string script;
    // r_lightScale's default.
    float lightScale = 3.0f;
    // Off, no light casts shadows (to judge occlusion in the bake).
    bool shadows = true;
    // Merge the static func_static brush models into the area models before the bake.
    bool staticModels = false;
    // With staticModels, merge the static func_statics' mesh models (.lwo, .ase) too, lit per vertex.
    bool meshModels = false;
    LightmapPolicy policy;
};

static bool ParseLightingJob(const JsonValue & value, WorldLightingJob & output)
{
    if (value.type != JsonValue::Type::Object)
    {
        return Fail("lighting must be an object");
    }

    for (const std::string & key : value.keys)
    {
        if (key != "script" && key != "light_scale" && key != "luxel_size" && key != "shadow_samples" && key != "chart_angle" &&
            key != "ray_offset" && key != "min_exposure" && key != "flat_steps" && key != "shadows" && key != "static_models" &&
            key != "mesh_models")
        {
            return Fail("unknown lighting field: " + key);
        }
    }

    if (const JsonValue * script = value.Find("script"))
    {
        if (!RelativeDataPath(script))
        {
            return Fail("lighting.script must be a relative path without '..'");
        }

        output.script = script->text;
    }

    struct Range
    {
        const char * key;
        double minimum;
        double maximum;
        float * target;
    };

    const Range ranges[] = {
        { "light_scale", 0.0, 16.0, &output.lightScale },
        { "luxel_size", 1.0, 256.0, &output.policy.luxelSize },
        { "chart_angle", 0.0, 89.0, &output.policy.chartAngle },
        { "ray_offset", 0.0, 16.0, &output.policy.rayOffset },
        { "min_exposure", 0.01, 1.99, &output.policy.minExposure },
    };

    for (const Range & range : ranges)
    {
        if (const JsonValue * number = value.Find(range.key))
        {
            if (number->type != JsonValue::Type::Number || number->number < range.minimum || number->number > range.maximum)
            {
                return Fail(std::string("lighting.") + range.key + " must be a number from " + Number(range.minimum) + " to " + Number(range.maximum));
            }

            *range.target = static_cast<float>(number->number);
        }
    }

    if (const JsonValue * samples = value.Find("shadow_samples"))
    {
        if (!samples->IsInteger(1, 4) || (samples->number != 1.0 && samples->number != 4.0))
        {
            return Fail("lighting.shadow_samples must be 1 or 4");
        }

        output.policy.shadowSamples = static_cast<int>(samples->number);
    }

    if (const JsonValue * shadows = value.Find("shadows"))
    {
        if (shadows->type != JsonValue::Type::Boolean)
        {
            return Fail("lighting.shadows must be true or false");
        }

        output.shadows = shadows->boolean;
    }

    if (const JsonValue * staticModels = value.Find("static_models"))
    {
        if (staticModels->type != JsonValue::Type::Boolean)
        {
            return Fail("lighting.static_models must be true or false");
        }

        output.staticModels = staticModels->boolean;
    }

    if (const JsonValue * meshModels = value.Find("mesh_models"))
    {
        if (meshModels->type != JsonValue::Type::Boolean)
        {
            return Fail("lighting.mesh_models must be true or false");
        }

        output.meshModels = meshModels->boolean;
    }

    if (const JsonValue * steps = value.Find("flat_steps"))
    {
        if (!steps->IsInteger(0, 16))
        {
            return Fail("lighting.flat_steps must be an integer from 0 to 16");
        }

        output.policy.flatSteps = static_cast<int>(steps->number);
    }

    output.enabled = true;
    return true;
}

static bool ParseWorldJob(const JsonValue & root, std::string & map, std::vector<std::string> & containers, WorldPolicy & policy,
                          WorldTextureJob & textures, WorldLightingJob & lighting)
{
    const JsonValue * version = root.Find("version");

    if (root.type != JsonValue::Type::Object || version == nullptr || !version->IsInteger(1, 1))
    {
        return Fail("world job must be an object with version 1");
    }

    for (const std::string & key : root.keys)
    {
        if (key != "version" && key != "map" && key != "containers" && key != "topology" && key != "shading" && key != "default_color" &&
            key != "material_tint" && key != "materials" && key != "textures" && key != "lighting")
        {
            return Fail("unknown world job field: " + key);
        }
    }

    const JsonValue * name = root.Find("map");

    // Without its extension: <map>.proc below --data, or generated/<map>.bproc in the containers.
    if (!RelativeDataPath(name))
    {
        return Fail("map must be a relative path without '..'");
    }

    map = name->text;
    containers.clear();

    // Retail maps: the .resources containers below --data that hold the binary cache, later ones
    // winning as the engine's lookup does.
    if (const JsonValue * list = root.Find("containers"))
    {
        if (list->type != JsonValue::Type::Array || list->items.empty())
        {
            return Fail("containers must be a non-empty array");
        }

        for (const JsonValue & item : list->items)
        {
            if (!RelativeDataPath(&item))
            {
                return Fail("containers must be relative paths without '..'");
            }

            containers.push_back(item.text);
        }
    }

    if (const JsonValue * topology = root.Find("topology"))
    {
        if (topology->type != JsonValue::Type::String || (topology->text != "hybrid" && topology->text != "strips"))
        {
            return Fail("topology must be \"hybrid\" or \"strips\"");
        }

        policy.topology = topology->text == "strips" ? WorldTopology::Strips : WorldTopology::IndexedStrips;
    }

    if (const JsonValue * shading = root.Find("shading"))
    {
        const JsonValue * ambient = shading->type == JsonValue::Type::Object ? shading->Find("ambient") : nullptr;

        if (shading->type != JsonValue::Type::Object || shading->keys.size() != 2 || !GetColor(shading->Find("light"), policy.light) ||
            ambient == nullptr || ambient->type != JsonValue::Type::Number || ambient->number < 0.0 || ambient->number > 1.0)
        {
            return Fail("shading must hold a light direction and an ambient level in [0, 1]");
        }

        policy.ambient = static_cast<float>(ambient->number);
    }

    if (root.Find("default_color") != nullptr && !GetColor(root.Find("default_color"), policy.defaultColor))
    {
        return Fail("default_color must be three numbers in [0, 2]");
    }

    if (const JsonValue * tint = root.Find("material_tint"))
    {
        if (tint->type != JsonValue::Type::Number || tint->number < 0.0 || tint->number > 1.0)
        {
            return Fail("material_tint must be a number in [0, 1]");
        }

        policy.tint = static_cast<float>(tint->number);
    }

    if (const JsonValue * texturesValue = root.Find("textures"))
    {
        if (!ParseTextureJob(*texturesValue, textures))
        {
            return false;
        }
    }

    if (const JsonValue * lightingValue = root.Find("lighting"))
    {
        if (!ParseLightingJob(*lightingValue, lighting))
        {
            return false;
        }

        // The pages travel in the D3TX; the bake reads the light materials the textures section names.
        if (!textures.enabled || containers.empty())
        {
            return Fail("lighting needs a textures section and the map's containers");
        }
    }

    if (const JsonValue * materials = root.Find("materials"))
    {
        if (materials->type != JsonValue::Type::Object)
        {
            return Fail("materials must map names to colours");
        }

        for (std::size_t index = 0; index < materials->keys.size(); ++index)
        {
            WorldMaterialColor entry;
            entry.material = materials->keys[index];

            if (!GetColor(&materials->items[index], entry.color))
            {
                return Fail("bad colour for material " + entry.material);
            }

            policy.colors.push_back(std::move(entry));
        }
    }

    return true;
}

static std::string Real(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", static_cast<double>(value));
    return text;
}

static std::string Bounds(const WorldBounds & bounds)
{
    return "[" + Real(bounds.min[0]) + ", " + Real(bounds.min[1]) + ", " + Real(bounds.min[2]) + ", " + Real(bounds.max[0]) + ", " +
           Real(bounds.max[1]) + ", " + Real(bounds.max[2]) + "]";
}

static std::string Ratio(std::size_t numerator, std::size_t denominator)
{
    return Number(denominator == 0 ? 0.0 : static_cast<double>(numerator) / static_cast<double>(denominator));
}

// The world's diffuse textures (RENDERING_ARCHITECTURE.md 10.4): each material's first diffuse
// stage, decoded from the BIMAGE id generated for it, its coverage image's alpha merged in when the
// stage alpha-tests, cooked into GSTX v2 and packed by material into <stem>.d3tx, with a report.
// log2 of each material's UV units per world unit over the map's area surfaces: the square root of
// its total UV area over its total world area (0 for a material without area).
static std::vector<float> MaterialUvDensity(const ProcFile & proc, const std::vector<std::string> & materials)
{
    std::map<std::string, std::array<double, 2> > totals;

    for (const ProcModel & model : proc.models)
    {
        if (model.name.rfind("_area", 0) != 0)
        {
            continue;
        }

        for (const ProcSurface & surface : model.surfaces)
        {
            std::array<double, 2> & total = totals[surface.material];

            for (std::size_t index = 0; index + 2 < surface.indices.size(); index += 3)
            {
                const ProcVertex & a = surface.vertices[surface.indices[index]];
                const ProcVertex & b = surface.vertices[surface.indices[index + 1]];
                const ProcVertex & c = surface.vertices[surface.indices[index + 2]];
                double edge1[3];
                double edge2[3];

                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    edge1[axis] = static_cast<double>(b.position[axis]) - static_cast<double>(a.position[axis]);
                    edge2[axis] = static_cast<double>(c.position[axis]) - static_cast<double>(a.position[axis]);
                }

                const double cx = edge1[1] * edge2[2] - edge1[2] * edge2[1];
                const double cy = edge1[2] * edge2[0] - edge1[0] * edge2[2];
                const double cz = edge1[0] * edge2[1] - edge1[1] * edge2[0];
                const double du1 = static_cast<double>(b.uv[0]) - static_cast<double>(a.uv[0]);
                const double dv1 = static_cast<double>(b.uv[1]) - static_cast<double>(a.uv[1]);
                const double du2 = static_cast<double>(c.uv[0]) - static_cast<double>(a.uv[0]);
                const double dv2 = static_cast<double>(c.uv[1]) - static_cast<double>(a.uv[1]);
                const double uvCross = du1 * dv2 - dv1 * du2;
                total[0] += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
                total[1] += 0.5 * std::fabs(uvCross);
            }
        }
    }

    std::vector<float> density;

    for (const std::string & material : materials)
    {
        const auto found = totals.find(material);
        const bool usable = found != totals.end() && found->second[0] > 0.0 && found->second[1] > 0.0;
        density.push_back(usable ? static_cast<float>(0.5 * std::log2(found->second[1] / found->second[0])) : 0.0f);
    }

    return density;
}

// Opens the material containers and the map's, later ones winning as the engine's lookup does.
static bool OpenMaterialCatalog(const fs::path & data, const std::vector<std::string> & containers, const WorldTextureJob & job,
                                ResourceCatalog & catalog)
{
    std::vector<fs::path> inputs;
    std::string error;

    for (const std::string & container : job.materialContainers)
    {
        inputs.push_back(data / container);
    }

    for (const std::string & container : containers)
    {
        inputs.push_back(data / container);
    }

    if (!catalog.Open(inputs, error))
    {
        return Fail("texture containers: " + error);
    }

    return true;
}

// Declarations in file-name order, the first definition of a name winning; tables likewise.
static bool ScanCatalogMaterials(ResourceCatalog & catalog, std::map<std::string, MaterialInfo> & declared, MaterialTables & tables)
{
    std::string error;

    for (const auto & entry : catalog.Entries())
    {
        if (entry.first.rfind("materials/", 0) != 0 || entry.first.size() < 4 || entry.first.compare(entry.first.size() - 4, 4, ".mtr") != 0)
        {
            continue;
        }

        std::vector<std::uint8_t> bytes;

        if (!catalog.Read(entry.first, bytes, error) ||
            !ScanMaterials(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()), declared, error, &tables))
        {
            return Fail(entry.first + ": " + error);
        }
    }

    return true;
}

// textureUsage_t's TD_BUMP, which id loads bump maps with.
static constexpr int kTextureUsageBump = 3;

static std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

// An image as the GS will sample it, back out of the cooked file, for the preview PNGs.
static bool WritePreviewPair(const fs::path & previews, const std::string & base, const RgbaImage & original, const GstxV2Texture & check)
{
    std::string error;
    RgbaImage preview;
    preview.Allocate(check.width, check.height);

    for (int y = 0; y < check.height; ++y)
    {
        for (int x = 0; x < check.width; ++x)
        {
            std::uint32_t value = 0;
            (void)SampleGstxV2(check, 0, x, y, value);
            std::uint8_t * texel = preview.At(x, y);
            texel[0] = static_cast<std::uint8_t>(value & 0xFFU);
            texel[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
            texel[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
            texel[3] = static_cast<std::uint8_t>(std::min(255U, (value >> 24) * 2U));
        }
    }

    if (!WritePng(previews / (base + ".original.png"), original, error) || !WritePng(previews / (base + ".cooked.png"), preview, error))
    {
        return Fail(error);
    }

    return true;
}

// The world textures: each material's base pass and its additive passes, every texture cooked once
// per image, alpha test and bump map; 'parms' are the materials' (World::materialParms).
// The order the package's texture files follow its tables in, for a target that streams them one at a
// time as the areas needing them come into view (world_textures.cpp): the lightmap pages and the sky's
// faces first (most views read them), then each area's textures as a walk outward through the portals
// from area 0 meets them, its batches in order; anything no area draws last.
static std::vector<std::uint32_t> StreamingOrder(const TexturePack & pack, const World & world)
{
    std::vector<std::uint32_t> order;
    std::vector<bool> placed(pack.textures.size(), false);
    const auto Place = [&](std::uint32_t texture)
    {
        if (texture < pack.textures.size() && !placed[texture])
        {
            placed[texture] = true;
            order.push_back(texture);
        }
    };

    for (const std::uint32_t page : pack.lightmapPages)
    {
        Place(page);
    }

    for (const D3txSky & sky : pack.skies)
    {
        for (const std::uint32_t face : sky.faces)
        {
            Place(face);
        }
    }

    std::vector<bool> visited(world.areas.size(), false);
    std::vector<std::uint32_t> queue;

    for (std::uint32_t root = 0; root < world.areas.size(); ++root)
    {
        if (visited[root])
        {
            continue;
        }

        visited[root] = true;
        queue.push_back(root);

        for (std::size_t next = queue.size() - 1; next < queue.size(); ++next)
        {
            const WorldArea & area = world.areas[queue[next]];

            for (std::uint32_t index = area.firstBatch; index < area.firstBatch + area.batchCount; ++index)
            {
                const std::uint16_t material = world.batches[index].material;
                Place(pack.materialTextures[material]);

                for (std::uint32_t pass = 0; pass < pack.materialPassCount[material]; ++pass)
                {
                    Place(pack.passes[pack.materialFirstPass[material] + pass].texture);
                }
            }

            for (const WorldPortal & portal : world.portals)
            {
                for (int side = 0; side < 2; ++side)
                {
                    const std::uint32_t other = portal.areas[1 - side];

                    if (portal.areas[side] == queue[next] && !visited[other])
                    {
                        visited[other] = true;
                        queue.push_back(other);
                    }
                }
            }
        }
    }

    for (std::uint32_t texture = 0; texture < pack.textures.size(); ++texture)
    {
        Place(texture);
    }

    return order;
}

static bool CookWorldTextures(ResourceCatalog & catalog, const std::map<std::string, MaterialInfo> & declared, const MaterialTables & tables,
                              const WorldTextureJob & job, const World & world, const std::vector<float> & density,
                              const std::vector<LightmapPage> & pages, const fs::path & output, const std::string & stem,
                              const fs::path & previews)
{
    const std::vector<std::string> & materials = world.materials;
    const std::vector<ShaderParms> & parms = world.materialParms;
    std::string error;
    TexturePack pack;
    pack.materialsHash = MaterialNamesHash(materials);
    pack.materialUvDensity = density;
    std::map<std::string, std::uint32_t> cooked;
    std::map<std::string, std::uint16_t> tableOffsets;
    std::string textureReport;
    std::string untextured;
    std::string passReport;
    std::size_t vramBytes = 0;
    std::size_t t4 = 0;
    std::size_t cavities = 0;
    std::size_t dynamicPasses = 0;

    if (!previews.empty())
    {
        std::error_code status;
        fs::create_directories(previews, status);

        if (status)
        {
            return Fail("cannot create " + previews.string());
        }
    }

    // Cooks 'image' (a generated BIMAGE name; a cube's 'face') once per key, under the job's policy
    // or 'custom'; 'prepare' adjusts the decoded pixels. Returns the texture's index, or
    // kD3txNoTexture when the image is unavailable.
    const auto CookImage = [&](const std::string & key, const std::string & image, const std::string & label, bool & coverage,
                               const std::function<void(RgbaImage &, bool &, RgbaImage &)> & prepare, std::uint32_t & index, int face = 0,
                               const TexturePolicy * custom = nullptr) -> bool
    {
        const auto previous = cooked.find(key);

        if (previous != cooked.end())
        {
            index = previous->second;
            coverage = (pack.textureFlags[index] & kD3txFlagAlphaTest) != 0;
            return true;
        }

        std::vector<std::uint8_t> bytes;
        RgbaImage pixels;

        if (!catalog.Read(image, bytes, error) || !DecodeBimage(bytes, 0, pixels, error, face))
        {
            index = kD3txNoTexture;
            return true;
        }

        coverage = false;
        RgbaImage reference;
        prepare(pixels, coverage, reference);

        CookedTexture texture;
        std::vector<std::uint8_t> file;
        GstxV2Texture check;
        TexturePolicy policy = custom != nullptr ? *custom : job.policy;
        policy.formatSource = reference.width > 0 ? &reference : nullptr;

        if (!CookTexture(pixels, policy, GstxUsage::Diffuse, texture, error) || !WriteGstxV2(texture.image, file, error) ||
            !ReadGstxV2(file, check, error))
        {
            return Fail(label + ": " + error);
        }

        index = static_cast<std::uint32_t>(pack.textures.size());
        cooked.emplace(key, index);
        pack.textures.push_back(std::move(file));
        pack.textureFlags.push_back(coverage ? kD3txFlagAlphaTest : 0U);
        vramBytes += static_cast<std::size_t>(check.blocks) * kGsBlockBytes;
        t4 += texture.image.psm == GsPsm::T4 ? 1U : 0U;
        textureReport += std::string(index == 0 ? "" : ",") + "\n    { \"image\": " + JsonString(image) + ", \"key\": " + JsonString(key) +
                         ", \"source\": [" + std::to_string(texture.sourceWidth) + ", " + std::to_string(texture.sourceHeight) + "], \"size\": [" +
                         std::to_string(texture.image.width) + ", " + std::to_string(texture.image.height) + "], \"psm\": \"" +
                         (texture.image.psm == GsPsm::T4 ? "PSMT4" : "PSMT8") + "\", \"levels\": " + std::to_string(texture.image.levels.size()) +
                         ", \"blocks\": " + std::to_string(check.blocks) + ", \"alpha_test\": " + (coverage ? "true" : "false") +
                         ", \"rmse\": " + Number(texture.quality.rmse) + ", \"max_error\": " + std::to_string(texture.quality.maxError) + " }";

        return previews.empty() || WritePreviewPair(previews, std::to_string(index) + "-" + fs::path(label).filename().string(), pixels, check);
    };

    const auto Opaque = [](RgbaImage & pixels)
    {
        for (int y = 0; y < pixels.height; ++y)
        {
            for (int x = 0; x < pixels.width; ++x)
            {
                pixels.At(x, y)[3] = 255;
            }
        }
    };

    // The map's sky: the first sky material's cube map, its six faces cooked at the job's sky size,
    // and the colour its stage draws them with (parms folded in, time at 0). Every later sky
    // material draws the same sky; one with another cube map or colour is noted in the report.
    std::string skyReport = "null";
    std::string skyNotes;
    std::string skyCube;
    std::size_t skyMaterials = 0;

    const auto CookSky = [&](const MaterialInfo & info, const ShaderParms & surfaceParms, const std::string & material) -> bool
    {
        const MaterialStage & stage = info.stages[static_cast<std::size_t>(info.skyStage)];
        const std::string cube = GeneratedImageName(stage.cubeMap, kTextureUsageDefault, stage.cameraCube ? kCubeFilesCamera : kCubeFilesNative);
        ExpressionInputs inputs;
        std::copy(surfaceParms.begin(), surfaceParms.end(), inputs.parms.begin());
        inputs.tables = &tables;
        std::array<std::uint8_t, 4> color = { 0x80, 0x80, 0x80, 0x80 };
        bool timed = false;

        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const float value = EvaluateExpression(stage.color[channel], inputs, 1.0f) * 128.0f;
            color[channel] = static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0f, 255.0f));
            timed = timed || (ExpressionReads(stage.color[channel]) & kExpressionReadsTime) != 0;
        }

        if (!pack.skies.empty())
        {
            if (cube != skyCube || color != pack.skies.front().color)
            {
                skyNotes += std::string(skyNotes.empty() ? "" : ", ") + JsonString(material + " draws the map's first sky, not its own (" + stage.cubeMap + ")");
            }

            return true;
        }

        TexturePolicy policy = job.policy;
        policy.scale = 1.0;
        policy.minExtent = job.skyExtent;
        policy.maxExtent = job.skyExtent;
        policy.minMipExtent = job.skyExtent;
        D3txSky sky;
        sky.color = color;
        const auto opaque = [&Opaque](RgbaImage & pixels, bool &, RgbaImage &)
        {
            Opaque(pixels);
        };

        for (std::size_t face = 0; face < kD3txSkyFaces; ++face)
        {
            bool coverage = false;
            const std::string label = stage.cubeMap + "-face" + std::to_string(face);

            if (!CookImage(cube + "#face" + std::to_string(face), cube, label, coverage, opaque, sky.faces[face], static_cast<int>(face), &policy))
            {
                return false;
            }

            // A cube BIMAGE holds all six faces or fails whole.
            if (sky.faces[face] == kD3txNoTexture)
            {
                return true;
            }
        }

        pack.skies.push_back(sky);
        skyCube = cube;
        skyReport = "{ \"material\": " + JsonString(material) + ", \"cube\": " + JsonString(cube) + ", \"size\": " + std::to_string(job.skyExtent) +
                    ", \"faces\": [";

        for (std::size_t face = 0; face < kD3txSkyFaces; ++face)
        {
            skyReport += (face == 0 ? "" : ", ") + std::to_string(sky.faces[face]);
        }

        skyReport += "], \"color\": [" + std::to_string(color[0]) + ", " + std::to_string(color[1]) + ", " + std::to_string(color[2]) + ", " +
                     std::to_string(color[3]) + "], \"time_fixed\": " + (timed ? "true" : "false") + " }";
        return true;
    };

    for (std::size_t materialIndex = 0; materialIndex < materials.size(); ++materialIndex)
    {
        const std::string & material = materials[materialIndex];
        const auto found = declared.find(Lowercase(material));
        // A sky stage on a material without a diffuse one: the sky box shows through its surfaces.
        const bool sky = found != declared.end() && found->second.skyStage >= 0 && found->second.diffuse.empty();
        const bool hidden = !sky && found != declared.end() && found->second.diffuse.empty() && found->second.translucent;
        const char * reason = found == declared.end()         ? "undeclared"
                              : sky                           ? nullptr
                              : hidden                        ? "translucent without a diffuse stage: not drawn textured"
                              : found->second.diffuse.empty() ? "no diffuse stage"
                                                              : nullptr;
        std::uint32_t index = hidden ? kD3txHidden : kD3txNoTexture;
        MaterialPassPlan plan;
        bool coverage = false;
        bool baked = false;

        if (sky)
        {
            if (!CookSky(found->second, parms[materialIndex], material))
            {
                return false;
            }

            index = pack.skies.empty() ? kD3txNoTexture : kD3txSky;
            reason = pack.skies.empty() ? "sky images unavailable" : nullptr;
            skyMaterials += pack.skies.empty() ? 0U : 1U;
        }
        else if (reason == nullptr)
        {
            const MaterialInfo & info = found->second;
            plan = PlanMaterialPasses(info, parms[materialIndex], tables);
            const std::string image = GeneratedImageName(info.diffuse, kTextureUsageDiffuse);
            const std::string bump = info.bump.empty() || job.cavity.amplitude <= 0.0f ? std::string() : GeneratedImageName(info.bump, kTextureUsageBump);
            const std::string key = image + (info.alphaTest ? "#alphatest" : "") + (bump.empty() ? "" : "#cavity:" + bump);

            const auto prepare = [&](RgbaImage & pixels, bool & hasCoverage, RgbaImage & reference)
            {
                std::vector<std::uint8_t> bytes;

                if (info.alphaTest)
                {
                    RgbaImage mask;
                    hasCoverage = catalog.Read(GeneratedImageName(info.diffuse, kTextureUsageCoverage), bytes, error) &&
                                  DecodeBimage(bytes, 0, mask, error) && mask.width == pixels.width && mask.height == pixels.height;

                    for (int y = 0; hasCoverage && y < pixels.height; ++y)
                    {
                        for (int x = 0; x < pixels.width; ++x)
                        {
                            pixels.At(x, y)[3] = mask.At(x, y)[3];
                        }
                    }
                }

                if (!hasCoverage)
                {
                    Opaque(pixels);
                }

                RgbaImage normal;

                if (!bump.empty() && catalog.Read(bump, bytes, error) && DecodeBimage(bytes, 0, normal, error))
                {
                    reference = pixels;
                    BakeCavity(pixels, normal, job.cavity);
                    baked = true;
                }
            };

            if (!CookImage(key, image, info.diffuse, coverage, prepare, index))
            {
                return false;
            }

            if (index == kD3txNoTexture)
            {
                reason = "diffuse image unavailable";
            }

            cavities += baked ? 1U : 0U;
        }

        if (reason != nullptr)
        {
            untextured += std::string(untextured.empty() ? "" : ",") + "\n    { \"material\": " + JsonString(material) + ", \"reason\": \"" + reason + "\" }";
        }

        pack.materialTextures.push_back(index);
        pack.materialAlphaRef.push_back(coverage ? plan.alphaRef : std::uint8_t(0));
        pack.materialFirstPass.push_back(static_cast<std::uint16_t>(pack.passes.size()));
        std::string passes;

        for (const PlannedPass & planned : index < kD3txSky ? plan.passes : std::vector<PlannedPass>())
        {
            const std::string label = material + " stage " + std::to_string(planned.stage);
            std::uint32_t texture = kD3txNoTexture;
            bool opaqueCoverage = false;
            const auto opaque = [&Opaque](RgbaImage & pixels, bool &, RgbaImage &)
            {
                Opaque(pixels);
            };

            // id loads a blend stage's image as TD_DEFAULT; some ship only as diffuse.
            for (const int usage : { kTextureUsageDefault, kTextureUsageDiffuse })
            {
                const std::string image = GeneratedImageName(planned.image, usage);

                if (texture == kD3txNoTexture && !CookImage(image, image, planned.image, opaqueCoverage, opaque, texture))
                {
                    return false;
                }
            }

            if (texture == kD3txNoTexture)
            {
                plan.skipped.push_back("stage " + std::to_string(planned.stage) + " (" + planned.image + "): image unavailable");
                continue;
            }

            AddPlannedPass(pack, planned, texture, tables, tableOffsets);
            const D3txPass & pass = pack.passes.back();
            dynamicPasses += planned.nodes.empty() ? 0U : 1U;
            passes += std::string(passes.empty() ? "" : ", ") + "{ \"stage\": " + std::to_string(planned.stage) + ", \"image\": " +
                      JsonString(planned.image) + ", \"color\": [" + std::to_string(pass.color[0]) + ", " + std::to_string(pass.color[1]) + ", " +
                      std::to_string(pass.color[2]) + ", " + std::to_string(pass.color[3]) + "], \"nodes\": " + std::to_string(pass.nodeCount) + " }";
        }

        pack.materialPassCount.push_back(static_cast<std::uint8_t>(pack.passes.size() - pack.materialFirstPass.back()));

        if (!passes.empty() || !plan.skipped.empty() || pack.materialAlphaRef.back() != 0 || baked)
        {
            std::string skipped;

            for (const std::string & entry : plan.skipped)
            {
                skipped += std::string(skipped.empty() ? "" : ", ") + JsonString(entry);
            }

            passReport += std::string(passReport.empty() ? "" : ",") + "\n    { \"material\": " + JsonString(material) + ", \"index\": " +
                          std::to_string(materialIndex) + ", \"alpha_ref\": " + std::to_string(pack.materialAlphaRef.back()) + ", \"cavity\": " +
                          (baked ? "true" : "false") + ", \"passes\": [" + passes + "], \"skipped\": [" + skipped + "] }";
        }
    }

    std::size_t pageBytes = 0;

    for (std::size_t index = 0; index < pages.size(); ++index)
    {
        GstxV2Image image;
        std::vector<std::uint8_t> file;
        GstxV2Texture check;

        if (!CookLightmapPage(pages[index].size, pages[index].texels, image, error) || !WriteGstxV2(image, file, error) ||
            !ReadGstxV2(file, check, error))
        {
            return Fail("lightmap page " + std::to_string(index) + ": " + error);
        }

        pack.lightmapPages.push_back(static_cast<std::uint32_t>(pack.textures.size()));
        pack.textures.push_back(std::move(file));
        pack.textureFlags.push_back(kD3txFlagLightmap);
        pageBytes += static_cast<std::size_t>(check.blocks) * kGsBlockBytes;

        if (!previews.empty())
        {
            RgbaImage preview;
            preview.Allocate(pages[index].size, pages[index].size);

            for (int y = 0; y < pages[index].size; ++y)
            {
                for (int x = 0; x < pages[index].size; ++x)
                {
                    // 0x80 (1.0) shows as mid grey.
                    const std::uint8_t value = pages[index].texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(pages[index].size) + static_cast<std::size_t>(x)];
                    std::uint8_t * texel = preview.At(x, y);
                    texel[0] = value;
                    texel[1] = value;
                    texel[2] = value;
                    texel[3] = 255;
                }
            }

            if (!WritePng(previews / ("lightmap-" + std::to_string(index) + ".png"), preview, error))
            {
                return Fail(error);
            }
        }
    }

    std::vector<std::uint8_t> file;
    TexturePack check;
    pack.fileOrder = StreamingOrder(pack, world);

    if (!WriteD3tx(pack, file, error) || !ReadD3tx(file, check, error))
    {
        return Fail("texture pack: " + error);
    }

    if (!WriteFile(output / (stem + ".d3tx"), file.data(), file.size()))
    {
        return false;
    }

    // The pages' texels as the GS reads them back out of the written package (the CLUT entry's
    // alpha, which the lightmap pass multiplies by): the retail gate's reference samples these.
    std::vector<std::uint8_t> lightmaps;
    const auto PutU32 = [&lightmaps](std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            lightmaps.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };

    PutU32(static_cast<std::uint32_t>(check.lightmapPages.size()));

    for (std::size_t index = 0; index < check.lightmapPages.size(); ++index)
    {
        GstxV2Texture page;

        if (!ReadGstxV2(check.textures[check.lightmapPages[index]], page, error))
        {
            return Fail("lightmap page " + std::to_string(index) + ": " + error);
        }

        PutU32(static_cast<std::uint32_t>(page.width));
        PutU32(static_cast<std::uint32_t>(page.height));

        for (int y = 0; y < page.height; ++y)
        {
            for (int x = 0; x < page.width; ++x)
            {
                std::uint32_t value = 0;

                if (!SampleGstxV2(page, 0, x, y, value))
                {
                    return Fail("lightmap page " + std::to_string(index) + " cannot be sampled");
                }

                lightmaps.push_back(static_cast<std::uint8_t>(value >> 24));
            }
        }
    }

    if (!WriteFile(output / (stem + ".lightmaps.raw"), lightmaps.data(), lightmaps.size()))
    {
        return false;
    }

    const std::size_t textured = static_cast<std::size_t>(std::count_if(pack.materialTextures.begin(), pack.materialTextures.end(),
                                                                        [](std::uint32_t texture) { return texture < kD3txSky; }));
    const std::string report = "{\n  \"materials\": " + std::to_string(materials.size()) + ",\n  \"textured_materials\": " +
                               std::to_string(textured) + ",\n  \"textures\": " + std::to_string(pack.textures.size()) + ",\n  \"psmt4\": " +
                               std::to_string(t4) + ",\n  \"vram_bytes\": " + std::to_string(vramBytes) + ",\n  \"cavity_baked\": " +
                               std::to_string(cavities) + ",\n  \"passes\": " + std::to_string(pack.passes.size()) + ",\n  \"dynamic_passes\": " +
                               std::to_string(dynamicPasses) + ",\n  \"expression_nodes\": " + std::to_string(pack.nodes.size()) +
                               ",\n  \"table_values\": " + std::to_string(pack.tableValues.size()) + ",\n  \"lightmap_pages\": " +
                               std::to_string(pages.size()) + ",\n  \"lightmap_vram_bytes\": " + std::to_string(pageBytes) + ",\n  \"file_bytes\": " +
                               std::to_string(file.size()) + ",\n  \"sky\": " + skyReport + ",\n  \"sky_materials\": " +
                               std::to_string(skyMaterials) + ",\n  \"sky_notes\": [" + skyNotes + "],\n  \"texture_list\": [" + textureReport +
                               "\n  ],\n  \"material_passes\": [" + passReport + "\n  ],\n  \"untextured\": [" + untextured + "\n  ]\n}\n";
    std::cout << stem << ".d3tx: " << pack.textures.size() << " textures for " << textured << " of " << materials.size() << " materials ("
              << cavities << " with cavity, " << skyMaterials << " showing the sky" << (pack.skies.empty() ? "" : " box") << "), "
              << pack.passes.size() << " passes (" << dynamicPasses << " driven by time), " << vramBytes
              << " bytes of VRAM, " << pages.size() << " lightmap pages (" << pageBytes << " bytes), " << file.size() << " bytes\n";
    return WriteFile(output / (stem + ".textures.json"), report.data(), report.size());
}

// id's _noFalloff: white, zero clamped, with a black border (Image_intrinsic.cpp).
static LightImage NoFalloffImage()
{
    RgbaImage image;
    image.Allocate(64, 16);

    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            const std::uint8_t value = x > 0 && x < 63 && y > 0 && y < 15 ? 255 : 0;
            std::uint8_t * texel = image.At(x, y);
            texel[0] = value;
            texel[1] = value;
            texel[2] = value;
            texel[3] = value;
        }
    }

    return LightImage::FromRgba(image);
}

// Classifies the map's lights, bakes the static ones and writes <stem>.lights.json.
// The map's entities and the light context (targets, the map script) the lighting job reads.
static bool ReadMapEntities(ResourceCatalog & catalog, const std::string & map, const WorldLightingJob & job, std::vector<MapEntity> & entities,
                            LightContext & context)
{
    std::string error;
    std::vector<std::uint8_t> bytes;

    if (!catalog.Read(map + ".map", bytes, error) ||
        !ParseMapEntities(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()), entities, error))
    {
        return Fail(map + ".map: " + error);
    }

    std::string script;

    if (!job.script.empty())
    {
        if (!catalog.Read(job.script, bytes, error))
        {
            return Fail(job.script + ": " + error);
        }

        script.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    context = MakeLightContext(entities, script);
    return true;
}

static bool BakeWorldLighting(ResourceCatalog & catalog, const std::map<std::string, MaterialInfo> & declared, const MaterialTables & tables,
                              const std::vector<MapEntity> & entities, const LightContext & context, const ProcFile & proc,
                              const WorldLightingJob & job, WorldLighting & lighting, std::vector<LightmapPage> & pages, const fs::path & output,
                              const std::string & stem)
{
    std::string error;
    std::map<std::string, LightImage> images;
    std::string imageError;

    // Generated BIMAGEs by name (level 0), decoded once; null when unavailable.
    const auto Image = [&](const std::string & name, int usage) -> const LightImage *
    {
        const std::string key = Lowercase(name) == "_nofalloff" ? std::string("_nofalloff") : GeneratedImageName(name, usage);
        const auto found = images.find(key);

        if (found != images.end())
        {
            return &found->second;
        }

        if (key == "_nofalloff")
        {
            return &images.emplace(key, NoFalloffImage()).first->second;
        }

        std::vector<std::uint8_t> file;
        RgbaImage pixels;
        std::string readError;

        if (!catalog.Read(key, file, readError) || !DecodeBimage(file, 0, pixels, readError))
        {
            imageError = key + ": " + readError;
            return nullptr;
        }

        return &images.emplace(key, LightImage::FromRgba(pixels)).first->second;
    };

    const auto DefaultFalloff = [&declared](const char * material)
    {
        const auto found = declared.find(material);
        return found != declared.end() ? found->second.lightFalloffImage : std::string();
    };

    const std::string pointFalloff = DefaultFalloff("lights/defaultpointlight");
    const std::string projectedFalloff = DefaultFalloff("lights/defaultprojectedlight");
    LightmapScene scene;
    scene.proc = &proc;
    std::array<std::size_t, static_cast<std::size_t>(LightClass::Count)> classes = {};
    std::map<std::string, std::size_t> reasons;
    std::size_t lights = 0;
    std::size_t refused = 0;
    std::size_t dark = 0;
    std::size_t shadowed = 0;
    std::string lightList;

    for (const MapEntity & entity : entities)
    {
        if (Lowercase(entity.String("classname")) != "light")
        {
            continue;
        }

        ++lights;
        RenderLight light;

        if (!ParseRenderLight(entity, light, error))
        {
            // id spawns it without a volume: nothing to light.
            ++refused;
            continue;
        }

        const auto found = declared.find(light.shader);
        const MaterialInfo * material = found != declared.end() ? &found->second : nullptr;
        std::string reason;
        const LightClass lightClass = ClassifyLight(entity, light, material, context, reason);
        ++classes[static_cast<std::size_t>(lightClass)];
        ++reasons[std::string(LightClassName(lightClass)) + ": " + reason];
        lightList += std::string(lightList.empty() ? "" : ",") + "\n    { \"name\": " + JsonString(light.name) + ", \"class\": \"" +
                     LightClassName(lightClass) + "\", \"shader\": " + JsonString(light.shader) + ", \"reason\": " + JsonString(reason) + " }";

        if (lightClass != LightClass::Static)
        {
            continue;
        }

        LightSource source;
        source.name = light.name;
        source.castsShadows = job.shadows && !light.noShadows;
        source.ambient = material->ambientLight;

        if (!DeriveLightVolume(light, source.volume, error))
        {
            return Fail(error);
        }

        source.stages = EvaluateLightStages(*material, light, &tables, job.lightScale);

        if (source.stages.empty())
        {
            ++dark;
            continue;
        }

        const std::string & falloff = !material->lightFalloffImage.empty() ? material->lightFalloffImage : light.pointLight ? pointFalloff : projectedFalloff;
        source.falloff = falloff.empty() ? nullptr : Image(falloff, kTextureUsageDefault);

        if (!falloff.empty() && source.falloff == nullptr)
        {
            return Fail("light " + light.name + " falloff " + imageError);
        }

        for (const LightStageColor & stage : source.stages)
        {
            const LightImage * image = Image(stage.image, kTextureUsageLight);

            if (image == nullptr)
            {
                return Fail("light " + light.name + " image " + imageError);
            }

            source.images.push_back(image);
        }

        shadowed += source.castsShadows ? 1U : 0U;
        scene.lights.push_back(std::move(source));
    }

    for (const ProcModel & model : proc.models)
    {
        for (const ProcSurface & surface : model.surfaces)
        {
            const auto found = declared.find(Lowercase(surface.material));
            SurfaceShading shading;

            if (found != declared.end())
            {
                shading.lit = found->second.lit;
                shading.castsShadow = found->second.castsShadow;
                shading.perforated = found->second.alphaTest;
            }

            scene.materials.emplace(surface.material, shading);
        }
    }

    // Brush entities left out of the area models shadow too: their inline models sit at the
    // entity's origin and axis.
    std::map<std::string, const ProcModel *> inlineModels;

    for (const ProcModel & model : proc.models)
    {
        inlineModels.emplace(Lowercase(model.name), &model);
    }

    std::size_t staticModels = 0;

    for (const MapEntity & entity : entities)
    {
        const auto found = inlineModels.find(Lowercase(entity.String("model")));

        if (Lowercase(entity.String("classname")) != "func_static" || found == inlineModels.end() || found->first.rfind("_area", 0) == 0)
        {
            continue;
        }

        Vec3 origin = {};
        std::array<Vec3, 3> axis = {};
        entity.Placement(origin, axis);

        ++staticModels;

        for (const ProcSurface & surface : found->second->surfaces)
        {
            const auto shading = scene.materials.find(surface.material);

            if (shading == scene.materials.end() || !shading->second.castsShadow)
            {
                continue;
            }

            for (std::size_t index = 0; index + 2 < surface.indices.size(); index += 3)
            {
                std::array<Vec3, 3> triangle = {};

                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto & local = surface.vertices[surface.indices[index + corner]].position;

                    for (std::size_t component = 0; component < 3; ++component)
                    {
                        triangle[corner][component] = origin[component] + local[0] * axis[0][component] + local[1] * axis[1][component] +
                                                      local[2] * axis[2][component];
                    }
                }

                scene.occluders.push_back(triangle);
            }
        }
    }

    LightmapStats stats;
    const auto started = std::chrono::steady_clock::now();

    if (!BakeLightmaps(scene, job.policy, lighting, pages, stats, error))
    {
        return Fail("lightmaps: " + error);
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::string classReport;

    for (std::size_t index = 0; index < classes.size(); ++index)
    {
        classReport += std::string(index == 0 ? "" : ", ") + "\"" + LightClassName(static_cast<LightClass>(index)) + "\": " + std::to_string(classes[index]);
    }

    std::string reasonReport;

    for (const auto & [reason, count] : reasons)
    {
        reasonReport += std::string(reasonReport.empty() ? "" : ",") + "\n    " + JsonString(reason) + ": " + std::to_string(count);
    }

    const std::string report = "{\n  \"lights\": " + std::to_string(lights) + ",\n  \"refused\": " + std::to_string(refused) + ",\n  \"classes\": { " +
                               classReport + " },\n  \"baked\": " + std::to_string(scene.lights.size()) + ",\n  \"baked_shadowed\": " +
                               std::to_string(shadowed) + ",\n  \"baked_dark\": " + std::to_string(dark) + ",\n  \"entity_occluder_models\": " +
                               std::to_string(staticModels) + ",\n  \"occluders\": " + std::to_string(stats.occluders) + ",\n  \"lit_surfaces\": " +
                               std::to_string(stats.litSurfaces) + ",\n  \"vertex_lit_surfaces\": " + std::to_string(stats.vertexLitSurfaces) +
                               ",\n  \"lights_per_lit_surface\": " + Ratio(stats.surfaceLights, stats.litSurfaces) +
                               ",\n  \"max_lights_per_surface\": " + std::to_string(stats.maxSurfaceLights) + ",\n  \"charts\": " +
                               std::to_string(stats.charts) + ",\n  \"point_charts\": " + std::to_string(stats.pointCharts) +
                               ",\n  \"scaled_charts\": " + std::to_string(stats.scaledCharts) + ",\n  \"texels\": " + std::to_string(stats.texels) +
                               ",\n  \"pages\": " + std::to_string(stats.pages) + ",\n  \"rays\": " + std::to_string(stats.rays) +
                               ",\n  \"clipped\": " + std::to_string(stats.clippedTexels) + ",\n  \"max_intensity\": " + Real(stats.maxIntensity) +
                               ",\n  \"seconds\": " + Number(seconds) + ",\n  \"reasons\": {" + reasonReport + "\n  },\n  \"light_list\": [" + lightList +
                               "\n  ]\n}\n";
    std::cout << stem << ".lights.json: " << lights << " lights, " << scene.lights.size() << " baked (" << shadowed << " shadowed) into "
              << stats.pages << " pages of " << stats.charts << " charts, " << stats.rays << " rays, " << Number(seconds) << " s\n";
    return WriteFile(output / (stem + ".lights.json"), report.data(), report.size());
}

// 'topology', when not empty, replaces the job's ("hybrid" or "strips", already checked).
static bool CookWorldFile(const fs::path & data, const fs::path & jobPath, const fs::path & output, const std::string & topology,
                          const fs::path & previews)
{
    std::string jobText;
    JsonValue root;
    std::string error;
    std::string map;
    std::vector<std::string> containers;
    WorldPolicy policy;
    WorldTextureJob textures;
    WorldLightingJob lightingJob;

    if (!ReadFile(jobPath, kMaxJobBytes, jobText))
    {
        return false;
    }

    if (!ParseJson(jobText, root, error))
    {
        return Fail(jobPath.string() + ": " + error);
    }

    if (!ParseWorldJob(root, map, containers, policy, textures, lightingJob))
    {
        return false;
    }

    if (!topology.empty())
    {
        policy.topology = topology == "strips" ? WorldTopology::Strips : WorldTopology::IndexedStrips;
    }

    const fs::path procPath = data / (map + ".proc");
    // What the report names as the cook's input, relative to --data or the containers.
    std::string source = map + ".proc";
    ProcFile proc;

    if (containers.empty())
    {
        std::string procText;

        if (!ReadFile(procPath, kMaxProcBytes, procText))
        {
            return false;
        }

        if (!ParseProc(procText, proc, error))
        {
            return Fail(procPath.string() + ": " + error);
        }
    }
    else
    {
        // The binary cache id's loader would read first (idRenderWorldLocal::InitFromMap).
        std::vector<fs::path> inputs;
        ResourceCatalog catalog;
        std::vector<std::uint8_t> bytes;
        const std::string name = "generated/" + map + ".bproc";

        for (const std::string & container : containers)
        {
            inputs.push_back(data / container);
        }

        source = name;

        if (!catalog.Open(inputs, error) || !catalog.Read(name, bytes, error))
        {
            return Fail(name + ": " + error);
        }

        if (!ParseBinaryProc(bytes, proc, error))
        {
            return Fail(name + ": " + error);
        }
    }

    std::error_code status;
    fs::create_directories(output, status);

    if (status)
    {
        return Fail("cannot create " + output.string());
    }

    const std::string stem = fs::path(map).filename().string();
    ResourceCatalog materialCatalog;
    std::map<std::string, MaterialInfo> declared;
    MaterialTables tables;
    WorldLighting lighting;
    std::vector<LightmapPage> pages;

    if (textures.enabled && (!OpenMaterialCatalog(data, containers, textures, materialCatalog) || !ScanCatalogMaterials(materialCatalog, declared, tables)))
    {
        return false;
    }

    std::vector<MapEntity> entities;
    LightContext context;
    StaticModelStats staticModels;

    if (lightingJob.enabled && !ReadMapEntities(materialCatalog, map, lightingJob, entities, context))
    {
        return false;
    }

    // The mesh models' census first (it reads the proc's areas before the merge changes them, which
    // its placement does not depend on), then the merge.
    MeshModelStats meshModels;
    const ResourceReader readModel = [&materialCatalog](const std::string & name, std::vector<std::uint8_t> & bytes, std::string & readError)
    {
        readError.clear();
        return materialCatalog.Find(name) != nullptr && materialCatalog.Read(name, bytes, readError);
    };

    if (lightingJob.staticModels && !CensusMeshModels(proc, entities, context, declared, readModel, meshModels, error))
    {
        return Fail("mesh models: " + error);
    }

    if (lightingJob.staticModels &&
        !MergeStaticModels(proc, entities, context, declared, staticModels, error, lightingJob.meshModels ? &readModel : nullptr))
    {
        return Fail("static models: " + error);
    }

    if (lightingJob.enabled &&
        !BakeWorldLighting(materialCatalog, declared, tables, entities, context, proc, lightingJob, lighting, pages, output, stem))
    {
        return false;
    }

    // The diffuse stage's colour registers under each surface's parms tint its vertex colours.
    if (textures.enabled)
    {
        policy.stageColor = [&declared, &tables](const std::string & material, const ShaderParms & parms)
        {
            const auto found = declared.find(Lowercase(material));
            return found == declared.end() ? std::array<float, 3>{ 1.0f, 1.0f, 1.0f } : PlanMaterialPasses(found->second, parms, tables).diffuseColor;
        };
    }

    World world;
    WorldCookStats stats;
    std::vector<std::uint8_t> file;
    World check;

    if (!CookWorld(proc, policy, world, stats, error, lightingJob.enabled ? &lighting : nullptr) || !WriteD3wd(world, file, error))
    {
        return Fail(procPath.string() + ": " + error);
    }

    // The target admits exactly what the reader admits; never publish a file it would refuse.
    if (!ReadD3wd(file, check, error))
    {
        return Fail("cooked world fails validation: " + error);
    }

    if (!WriteFile(output / (stem + ".d3wd"), file.data(), file.size()))
    {
        return false;
    }

    std::string report = "{\n  \"map\": " + JsonString(map) + ",\n  \"source\": " + JsonString(source) +
                         ",\n  \"topology\": \"" +
                         (policy.topology == WorldTopology::Strips ? std::string("strips") : std::string("hybrid")) + "\",\n";
    report += "  \"areas\": " + std::to_string(world.areas.size()) + ",\n  \"surfaces\": " + std::to_string(world.surfaces.size()) +
              ",\n  \"materials\": " + std::to_string(world.materials.size()) + ",\n  \"portals\": " + std::to_string(world.portals.size()) +
              ",\n  \"nodes\": " + std::to_string(world.nodes.size()) + ",\n  \"skipped_models\": " + std::to_string(stats.skippedModels) +
              ",\n  \"triangles\": " + std::to_string(stats.triangles) + ",\n  \"batches\": " + std::to_string(stats.batches) +
              ",\n  \"strips\": " + std::to_string(stats.strips) + ",\n  \"transformed_vertices\": " + std::to_string(stats.transformedVertices) +
              ",\n  \"emitted_vertices\": " + std::to_string(stats.emittedVertices) +
              ",\n  \"triangles_per_strip\": " + Ratio(stats.triangles, stats.strips) +
              ",\n  \"transformed_per_triangle\": " + Ratio(stats.transformedVertices, stats.triangles) +
              ",\n  \"emitted_per_triangle\": " + Ratio(stats.emittedVertices, stats.triangles) +
              ",\n  \"triangles_per_batch\": " + Ratio(stats.triangles, stats.batches) +
              ",\n  \"lit_batches\": " + std::to_string(stats.litBatches) + ",\n  \"lightmap_pages\": " + std::to_string(world.lightmapPages) +
              ",\n  \"packet_bytes\": " + std::to_string(stats.packetBytes) + ",\n  \"file_bytes\": " + std::to_string(file.size()) +
              ",\n  \"max_position_error\": " + Real(stats.maxPositionError) + ",\n  \"max_uv_error\": " + Real(stats.maxUvError) + ",\n";

    if (lightingJob.staticModels)
    {
        std::string kept;

        for (const auto & [reason, count] : staticModels.kept)
        {
            kept += std::string(kept.empty() ? "" : ", ") + JsonString(reason) + ": " + std::to_string(count);
        }

        std::string meshKept;

        for (const auto & [reason, count] : staticModels.meshKept)
        {
            meshKept += std::string(meshKept.empty() ? "" : ", ") + JsonString(reason) + ": " + std::to_string(count);
        }

        report += "  \"static_models\": { \"inline\": " + std::to_string(staticModels.inlineModels) + ", \"merged\": " +
                  std::to_string(staticModels.merged) + ", \"partial\": " + std::to_string(staticModels.partial) + ", \"kept_surfaces\": " +
                  std::to_string(staticModels.keptSurfaces) + ", \"parm_surfaces\": " + std::to_string(staticModels.parmSurfaces) + ", \"parm_sets\": " +
                  std::to_string(staticModels.parmSets) + ", \"triangles\": " + std::to_string(staticModels.triangles) + ", \"solid_triangles\": " +
                  std::to_string(staticModels.solidTriangles) + ", \"surfaces_added\": " + std::to_string(staticModels.surfacesAdded) +
                  ", \"kept\": { " + kept + " }, \"mesh_models\": " + std::to_string(staticModels.meshModels) + ", \"mesh_merged\": " +
                  std::to_string(staticModels.meshMerged) + ", \"mesh_partial\": " + std::to_string(staticModels.meshPartial) +
                  ", \"mesh_triangles\": " + std::to_string(staticModels.meshTriangles) + ", \"mesh_solid_triangles\": " +
                  std::to_string(staticModels.meshSolidTriangles) + ", \"mesh_kept\": { " + meshKept + " }, \"merged_entities\": " +
                  std::to_string(staticModels.mergedEntities) + " },\n";

        const auto CountJson = [](const MeshModelStats::Count & count)
        {
            return "{ \"entities\": " + std::to_string(count.entities) + ", \"triangles\": " + std::to_string(count.triangles) +
                   ", \"vertices\": " + std::to_string(count.vertices) + " }";
        };
        std::string classes;
        std::string refused;
        std::string areas;

        for (const auto & [name, count] : meshModels.classes)
        {
            classes += std::string(classes.empty() ? "" : ", ") + JsonString(name) + ": " + CountJson(count);
        }

        for (const auto & [reason, count] : meshModels.kept)
        {
            refused += std::string(refused.empty() ? "" : ", ") + JsonString(reason) + ": " + CountJson(count);
        }

        for (const std::size_t triangles : meshModels.areaTriangles)
        {
            areas += std::string(areas.empty() ? "" : ", ") + std::to_string(triangles);
        }

        report += "  \"mesh_models\": { \"models\": " + std::to_string(meshModels.models) + ", \"model_triangles\": " +
                  std::to_string(meshModels.modelTriangles) + ", \"model_bytes\": " + std::to_string(meshModels.modelBytes) + ", \"missing\": " +
                  std::to_string(meshModels.missing) + ", \"classes\": { " + classes + " }, \"static\": " + CountJson(meshModels.staticMeshes) +
                  ", \"kept\": { " + refused + " }, \"translucent_triangles\": " + std::to_string(meshModels.translucentTriangles) +
                  ", \"parm_triangles\": " + std::to_string(meshModels.parmTriangles) + ", \"undeclared_triangles\": " +
                  std::to_string(meshModels.undeclaredTriangles) + ", \"solid_triangles\": " + std::to_string(meshModels.solidTriangles) +
                  ", \"largest\": " + JsonString(meshModels.largest) + ", \"largest_triangles\": " + std::to_string(meshModels.largestTriangles) +
                  ", \"area_triangles\": [" + areas + "] },\n";
        std::cout << stem << " mesh models: " << meshModels.models << " files (" << meshModels.modelTriangles << " triangles), "
                  << meshModels.staticMeshes.entities << " static func_static entities placing " << meshModels.staticMeshes.triangles
                  << " triangles, " << meshModels.missing << " missing\n";
    }

    report += "  \"area_list\": [";

    for (std::size_t index = 0; index < world.areas.size(); ++index)
    {
        const WorldArea & area = world.areas[index];
        std::size_t triangles = 0;

        for (std::uint32_t surface = area.firstSurface; surface < area.firstSurface + area.surfaceCount; ++surface)
        {
            triangles += world.surfaces[surface].indexCount / 3;
        }

        report += std::string(index == 0 ? "" : ",") + "\n    { \"surfaces\": " + std::to_string(area.surfaceCount) + ", \"batches\": " +
                  std::to_string(area.batchCount) + ", \"triangles\": " + std::to_string(triangles) + ", \"bounds\": " + Bounds(area.bounds) + " }";
    }

    // The material names in the file's order: test packages declare them until the cook compiles
    // material passes (R2).
    report += "\n  ],\n  \"material_names\": [";

    for (std::size_t index = 0; index < world.materials.size(); ++index)
    {
        report += std::string(index == 0 ? "" : ",") + "\n    " + JsonString(world.materials[index]);
    }

    report += "\n  ]\n}\n";
    std::cout << stem << ".d3wd: " << world.areas.size() << " areas, " << stats.triangles << " triangles in " << stats.batches << " batches, "
              << file.size() << " bytes\n";

    if (!WriteFile(output / (stem + ".report.json"), report.data(), report.size()))
    {
        return false;
    }

    return !textures.enabled ||
           CookWorldTextures(materialCatalog, declared, tables, textures, world, MaterialUvDensity(proc, world.materials), pages, output, stem, previews);
}

static void Usage()
{
    std::cout << "Usage: asset_reexport ui --data DIR --job FILE.json --output DIR [--previews DIR]\n"
                 "       asset_reexport world --data DIR --job FILE.json --output DIR [--topology hybrid|strips] [--previews DIR]\n"
                 "ui converts the retail interface textures a job lists into paletted GSTX files.\n"
                 "--data names the retail installation holding the job's .resources containers.\n"
                 "--output receives one file per texture and report.json; existing files are replaced.\n"
                 "--previews also writes <output>.original.png and <output>.converted.png per texture.\n"
                 "world cooks <data>/<map>.proc, the map the job names, into <output>/<name>.d3wd and\n"
                 "<name>.report.json; with containers listed, it reads generated/<map>.bproc from them.\n"
                 "--topology replaces the job's batch topology. A job with a textures section also cooks\n"
                 "each material's diffuse image into <name>.d3tx and <name>.textures.json; --previews then\n"
                 "writes each texture's original and cooked level 0 as PNGs.\n";
}

static int Run(int argc, char ** argv)
{
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")
    {
        Usage();
        return argc < 2 ? 2 : 0;
    }

    const std::string command(argv[1]);

    if (command != "ui" && command != "world")
    {
        Fail("unknown command: " + command);
        return 2;
    }

    fs::path data;
    fs::path job;
    fs::path output;
    fs::path previews;
    fs::path topology;

    for (int index = 2; index < argc; ++index)
    {
        const std::string argument(argv[index]);
        fs::path * target = argument == "--data"                         ? &data
                            : argument == "--job"                        ? &job
                            : argument == "--output"                     ? &output
                            : argument == "--previews"                   ? &previews
                            : argument == "--topology" && command == "world" ? &topology
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

    if (command == "world")
    {
        if (!topology.empty() && topology != "hybrid" && topology != "strips")
        {
            Fail("--topology must be hybrid or strips");
            return 2;
        }

        return CookWorldFile(data, job, output, topology.string(), previews) ? 0 : 1;
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
