// ================================================================================================
// File: ui_page_image_tests.cpp
// Brief: Check shared page ownership, transactional reads and bounded allocations with stubbed GS IO.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/page_image.h"

#include <idlib/precompiled.h>

#include <bit>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

struct Texture
{
    ps2::renderer::tex::Handle handle;
    void * pixels = nullptr;
};

static Texture s_textures[8] = {};
static u32 s_textureLimit = 8;
static u32 s_nextTexture = 0;
static u32 s_draws = 0;
static u32 s_lastPixel = 0;
static u32 s_fences = 0;
static bool s_device = true;
static bool s_frame = true;

static Texture * Find(ps2::renderer::tex::Handle handle)
{
    for (auto & texture : s_textures)
    {
        if (texture.handle.id == handle.id && handle.id != 0)
        {
            return &texture;
        }
    }

    return nullptr;
}

static u32 TextureCount()
{
    u32 count = 0;

    for (const auto & texture : s_textures)
    {
        count += texture.pixels != nullptr ? 1U : 0U;
    }

    return count;
}

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI page cache failed: %s\n", name);
    }

    return passed;
}

static bool WriteWord(std::FILE * file, u32 value)
{
    const u8 bytes[] = { static_cast<u8>(value), static_cast<u8>(value >> 8U),
                         static_cast<u8>(value >> 16U), static_cast<u8>(value >> 24U) };
    return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static bool Package(const char * path, u32 first, u32 second = 0, bool corrupt = false)
{
    std::FILE * const file = std::fopen(path, "wb");

    if (file == nullptr)
    {
        return false;
    }

    const u32 count = second != 0 ? 2U : 1U;
    const u32 words[] = { 0x47504955U, 1U, count * 254U, 254U, 256U, 1U, count, 0U };
    bool passed = true;

    for (const u32 word : words)
    {
        passed = WriteWord(file, word) && passed;
    }

    u32 pixels[ps2::ui::kPageBytes / sizeof(u32)] = {};

    for (u32 page = 0; page < count; ++page)
    {
        for (auto & pixel : pixels)
        {
            pixel = page == 0 ? first : second;
        }

        const u32 offset = ps2::ui::kPageHeaderBytes + count * ps2::ui::kPageRecordBytes + page * ps2::ui::kPageBytes;
        passed = WriteWord(file, offset) && passed;
        passed = WriteWord(file, ps2::ui::PageChecksum(pixels, sizeof(pixels))) && passed;
    }

    for (u32 page = 0; page < count; ++page)
    {
        for (auto & pixel : pixels)
        {
            pixel = (page == 0 ? first : second) ^ (corrupt ? 1U : 0U);
        }

        passed = std::fwrite(pixels, 1, sizeof(pixels), file) == sizeof(pixels) && passed;
    }

    return std::fclose(file) == 0 && passed;
}

static bool Draw(ps2::ui::PageImage & image, bool second = false)
{
    const float s0 = second ? 0.6f : 0.0f;
    const float s1 = image.GetHeader().columns == 1 || second ? 1.0f : 0.4f;
    const ps2::ui::Vertex vertices[] = {
        { 0.0f, 0.0f, s0, 0.0f, 255, 255, 255, 255 },
        { 80.0f, 0.0f, s1, 0.0f, 255, 255, 255, 255 },
        { 0.0f, 80.0f, s0, 1.0f, 255, 255, 255, 255 }
    };
    return image.DrawTriangle(vertices, ps2::renderer::gs::BlendMode::Replace);
}

static bool OuterFontGutters(const char * path)
{
    const auto baseline = ps2::system::heap::GetTotalStats();
    ps2::ui::PageCache cache;
    ps2::ui::PageImage image;
    bool passed = Check(cache.Init(1), "outer gutter cache startup");
    passed = Check(image.Open(path, cache, true), "outer gutter image admission") && passed;
    const float lowerS = -0.5f / static_cast<float>(image.GetHeader().width);
    const float lowerT = -0.5f / static_cast<float>(image.GetHeader().height);
    const ps2::ui::Vertex vertices[] = {
        { 10.0f, 10.0f, lowerS, lowerT, 255, 255, 255, 255 },
        { 30.0f, 10.0f, 0.02f, lowerT, 255, 255, 255, 255 },
        { 10.0f, 30.0f, lowerS, 0.04f, 255, 255, 255, 255 }
    };
    passed = Check(image.ValidateTriangle(vertices) &&
                   image.DrawTriangle(vertices, ps2::renderer::gs::BlendMode::Replace) &&
                   s_lastPixel == 0xFF0000FFU && cache.GetStats().loads == 1,
                   "negative outer font UVs retain first page ownership") &&
             passed;
    image.Close();
    passed = Check(image.Open(path, cache) && !image.ValidateTriangle(vertices),
                   "closing and reopening resets outer gutter admission") &&
             passed;
    image.Close();
    cache.Shutdown();
    const auto after = ps2::system::heap::GetTotalStats();
    return Check(after.requestedBytes == baseline.requestedBytes &&
                 after.allocationCount == baseline.allocationCount && TextureCount() == 0,
                 "outer gutter cleanup") &&
           passed;
}

static bool SharedCache(const char * firstPath, const char * secondPath, const char * corruptPath)
{
    using ps2::ui::PageCache;
    using ps2::ui::PageImage;
    const auto baseline = ps2::system::heap::GetTotalStats();
    PageCache cache;
    PageImage first;
    PageImage second;
    PageImage corrupt;
    bool passed = Check(cache.Init(2), "shared cache startup");
    passed = Check(first.Open(firstPath, cache) && second.Open(secondPath, cache) &&
                   corrupt.Open(corruptPath, cache),
                   "shared attachments") &&
             passed;
    const auto warm = ps2::system::heap::GetTotalStats();
    const auto opened = cache.GetStats();
    passed = Check(opened.images == 3 && opened.metadataBytes == 32 && opened.cacheBytes == 524288 &&
                   opened.scratchBytes == 262144 && TextureCount() == 2,
                   "one cache and scratch across three images") &&
             passed;
    passed = Check(first.GetStats().cacheBytes == 0 && first.GetStats().scratchBytes == 0 &&
                   first.GetStats().metadataBytes == 16 && first.GetRecords() != nullptr,
                   "image-local metadata ownership") &&
             passed;

    passed = Check(Draw(first) && s_lastPixel == 0xFF0000FFU, "first image page zero") && passed;
    passed = Check(Draw(second) && s_lastPixel == 0xFF00FF00U, "distinct image page zero") && passed;
    passed = Check(Draw(first) && s_lastPixel == 0xFF0000FFU, "image-keyed hit") && passed;
    passed = Check(Draw(first, true) && s_lastPixel == 0xFF00FFFFU, "LRU page replacement") && passed;
    passed = Check(Draw(second) && s_lastPixel == 0xFF00FF00U, "second image reload") && passed;
    const auto beforeBad = cache.GetStats();
    passed = Check(beforeBad.loads == 4 && beforeBad.hits == 1 && beforeBad.evictions == 2 &&
                   beforeBad.triangles == 5 && first.GetStats().loads == 2 &&
                   first.GetStats().hits == 1 && first.GetStats().triangles == 3,
                   "aggregate and image-local counters") &&
             passed;
    const u32 draws = s_draws;
    passed = Check(!Draw(corrupt), "bad payload rejected") && passed;
    const auto afterBad = cache.GetStats();
    passed = Check(afterBad.loads == beforeBad.loads && afterBad.evictions == beforeBad.evictions &&
                   s_draws == draws,
                   "bad read preserves cached pages and draws") &&
             passed;
    passed = Check(Draw(second) && s_lastPixel == 0xFF00FF00U &&
                   cache.GetStats().hits == beforeBad.hits + 1,
                   "bad read preserves existing hit") &&
             passed;
    const auto live = ps2::system::heap::GetTotalStats();
    passed = Check(live.requestedBytes == warm.requestedBytes && live.allocationCount == warm.allocationCount,
                   "draws allocate no memory") &&
             passed;

    auto invalid = ps2::ui::Vertex{ 0.0f, 0.0f, 0.0f, 0.0f, 255, 255, 255, 255 };
    invalid.s = std::bit_cast<float>(0x7F800000U);
    const ps2::ui::Vertex invalidTriangle[] = { invalid, invalid, invalid };
    passed = Check(!first.ValidateTriangle(invalidTriangle), "nonportable input preflight") && passed;
    s_frame = false;
    passed = Check(!Draw(first) && !first.ValidateTriangle(invalidTriangle), "frame preconditions") && passed;
    s_frame = true;

    const u32 fences = s_fences;
    first.Close();
    passed = Check(s_fences == fences + 1 && cache.GetStats().images == 2 && TextureCount() == 2,
                   "detach fences and retains shared slots") &&
             passed;
    const u32 loads = cache.GetStats().loads;
    passed = Check(first.Open(secondPath, cache) && Draw(first) && s_lastPixel == 0xFF00FF00U &&
                   cache.GetStats().loads == loads + 1,
                   "reused image identity cannot hit old key") &&
             passed;
    first.Close();
    second.Close();
    corrupt.Close();
    passed = Check(cache.GetStats().images == 0 && cache.GetStats().metadataBytes == 0,
                   "all image metadata detached") &&
             passed;
    cache.Shutdown();
    cache.Shutdown();
    const auto after = ps2::system::heap::GetTotalStats();
    return Check(after.requestedBytes == baseline.requestedBytes &&
                 after.allocationCount == baseline.allocationCount && TextureCount() == 0,
                 "shared shutdown returns baseline") &&
           passed;
}

static bool BoundsAndFailures(const char * path)
{
    using ps2::ui::PageCache;
    using ps2::ui::PageImage;
    const auto baseline = ps2::system::heap::GetTotalStats();
    PageCache cache;
    bool passed = Check(!cache.Init(0) && !cache.Init(9), "slot bounds");
    passed = Check(!cache.Init(1, { ps2::renderer::tex::Filter::Nearest, ps2::renderer::tex::Wrap::Repeat }) &&
                   !cache.Init(1, { ps2::renderer::tex::Filter::Linear, ps2::renderer::tex::Wrap::Clamp, ps2::renderer::tex::Wrap::Repeat }) &&
                   !cache.Init(1, { static_cast<ps2::renderer::tex::Filter>(255) }) && TextureCount() == 0,
                   "gutter pages require valid nearest or linear clamp sampling") &&
             passed;
    s_device = false;
    passed = Check(!cache.Init(), "device precondition") && passed;
    s_device = true;
    s_textureLimit = 1;
    passed = Check(!cache.Init(2) && !cache.IsInitialized() && TextureCount() == 0,
                   "partial initialization releases reserved slots") &&
             passed;
    s_textureLimit = 8;
    passed = Check(cache.Init(1), "startup after failed initialization") && passed;
    passed = Check(!cache.Init(1) && cache.IsInitialized(), "duplicate startup preserves owner") && passed;
    PageImage images[33];

    for (u32 index = 0; index < 32; ++index)
    {
        passed = Check(images[index].Open(path, cache), "bounded attached image admission") && passed;
    }

    passed = Check(!images[32].Open(path, cache) && cache.GetStats().images == 32,
                   "attached image limit retains previous owners") &&
             passed;
    passed = Check(!images[0].Open(path, cache) && images[0].IsOpen(), "duplicate image open") && passed;

    for (auto & image : images)
    {
        image.Close();
    }

    cache.Shutdown();
    PageImage owned;
    passed = Check(!owned.Open("/does/not/exist.uipg") && TextureCount() == 0,
                   "failed private open releases cache") &&
             passed;
    passed = Check(owned.Open(path) && owned.GetStats().cacheBytes == 524288 &&
                   owned.GetStats().scratchBytes == 262144 && Draw(owned),
                   "standalone owned cache compatibility") &&
             passed;
    owned.Close();
    const auto after = ps2::system::heap::GetTotalStats();
    return Check(after.requestedBytes == baseline.requestedBytes &&
                 after.allocationCount == baseline.allocationCount && TextureCount() == 0,
                 "failure and bound cleanup returns baseline") &&
           passed;
}

static bool ShutdownPrecondition(const char * path)
{
    const pid_t child = fork();

    if (child == 0)
    {
        ps2::ui::PageCache cache;
        ps2::ui::PageImage image;

        if (!cache.Init(1) || !image.Open(path, cache))
        {
            _exit(2);
        }

        cache.Shutdown();
        _exit(3);
    }

    int status = 0;
    return Check(child > 0 && waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
                 WTERMSIG(status) == SIGABRT,
                 "live attachment shutdown remains fatal");
}

} // namespace

namespace ps2::renderer::tex
{

Handle Create(int width, int height, Sampler sampler, const void * rgba, size_t sourceBytes, size_t strideBytes)
{
    Layout layout = {};

    if (TextureCount() == s_textureLimit || !MakeLayout(width, height, sampler, layout) ||
        sourceBytes != layout.bytes || strideBytes != static_cast<size_t>(width) * 4U)
    {
        return {};
    }

    for (auto & texture : s_textures)
    {
        if (texture.pixels == nullptr)
        {
            texture.pixels = system::heap::Alloc(sourceBytes, TAG_RENDER, 64);
            std::memcpy(texture.pixels, rgba, sourceBytes);
            texture.handle.id = ++s_nextTexture;
            return texture.handle;
        }
    }

    return {};
}

bool Update(Handle handle, const void * rgba, size_t sourceBytes, size_t strideBytes)
{
    Texture * const texture = Find(handle);

    if (texture == nullptr || sourceBytes != ui::kPageBytes || strideBytes != ui::kPageExtent * 4U)
    {
        return false;
    }

    std::memcpy(texture->pixels, rgba, sourceBytes);
    return true;
}

void Destroy(Handle handle)
{
    Texture * const texture = Find(handle);

    if (texture == nullptr)
    {
        FatalError("invalid stub texture destroy");
    }

    system::heap::Free(texture->pixels);
    *texture = {};
}

} // namespace ps2::renderer::tex

namespace ps2::renderer
{

bool IsInitialized()
{
    return s_device;
}

bool FrameStarted()
{
    return s_frame;
}

bool TexturedTriangleFixed(renderer::tex::Handle handle, const ui::FixedTriangle &, renderer::gs::BlendMode)
{
    const Texture * const texture = Find(handle);

    if (texture == nullptr)
    {
        return false;
    }

    std::memcpy(&s_lastPixel, texture->pixels, sizeof(s_lastPixel));
    ++s_draws;
    return true;
}

} // namespace ps2::renderer

namespace ps2::renderer::gs
{

int Width()
{
    return 640;
}

int Height()
{
    return 480;
}

} // namespace ps2::renderer::gs

namespace ps2::ui
{

void Flush()
{
}

} // namespace ps2::ui

namespace ps2::renderer::gs
{

void Kick()
{
}

void WaitIdle()
{
    ++s_fences;
}

} // namespace ps2::renderer::gs

int main()
{
    char directory[] = "/tmp/d3bfg-ui-pages-XXXXXX";

    if (mkdtemp(directory) == nullptr)
    {
        return 2;
    }

    char first[256] = {};
    char second[256] = {};
    char corrupt[256] = {};
    std::snprintf(first, sizeof(first), "%s/first.uipg", directory);
    std::snprintf(second, sizeof(second), "%s/second.uipg", directory);
    std::snprintf(corrupt, sizeof(corrupt), "%s/corrupt.uipg", directory);
    bool passed = Package(first, 0xFF0000FFU, 0xFF00FFFFU) &&
                  Package(second, 0xFF00FF00U) && Package(corrupt, 0xFFFF0000U, 0, true);

    if (passed)
    {
        passed = SharedCache(first, second, corrupt) && passed;
        passed = OuterFontGutters(first) && passed;
        passed = BoundsAndFailures(first) && passed;
        passed = ShutdownPrecondition(first) && passed;
    }

    std::remove(first);
    std::remove(second);
    std::remove(corrupt);
    rmdir(directory);
    return passed ? 0 : 1;
}
