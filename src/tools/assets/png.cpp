// ================================================================================================
// File: png.cpp
// Brief: Write RGBA images as PNG files for visual inspection of converted textures, with stored
// deflate blocks so the tools need no compression library.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/png.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace assets
{
namespace
{

static std::uint32_t Crc32(const std::vector<std::uint8_t> & bytes)
{
    static const std::array<std::uint32_t, 256> kTable = []
    {
        std::array<std::uint32_t, 256> table{};

        for (std::uint32_t entry = 0; entry < 256; ++entry)
        {
            std::uint32_t value = entry;

            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1) : value >> 1;
            }

            table[entry] = value;
        }

        return table;
    }();

    std::uint32_t crc = 0xFFFFFFFFU;

    for (const std::uint8_t byte : bytes)
    {
        crc = kTable[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
    }

    return ~crc;
}

static void PutBe32(std::vector<std::uint8_t> & bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

static void Chunk(std::vector<std::uint8_t> & file, const char (&type)[5], const std::vector<std::uint8_t> & data)
{
    PutBe32(file, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    file.insert(file.end(), body.begin(), body.end());
    PutBe32(file, Crc32(body));
}

} // namespace

bool WritePng(const std::filesystem::path & path, const RgbaImage & image, std::string & error)
{
    // Scanlines with filter byte 0, wrapped in zlib stored blocks and an Adler-32 trailer.
    std::vector<std::uint8_t> raw;
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4U;

    for (int y = 0; y < image.height; ++y)
    {
        raw.push_back(0);
        const std::uint8_t * row = image.At(0, y);
        raw.insert(raw.end(), row, row + rowBytes);
    }

    std::vector<std::uint8_t> zlib = { 0x78, 0x01 };
    std::uint32_t low = 1;
    std::uint32_t high = 0;

    for (const std::uint8_t byte : raw)
    {
        low = (low + byte) % 65521U;
        high = (high + low) % 65521U;
    }

    std::size_t offset = 0;

    do
    {
        const std::size_t size = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + size >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(size & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>(size >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~size & 0xFFU));
        zlib.push_back(static_cast<std::uint8_t>((~size >> 8) & 0xFFU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    } while (offset < raw.size());

    PutBe32(zlib, (high << 16) | low);
    std::vector<std::uint8_t> file = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<std::uint8_t> header;
    PutBe32(header, static_cast<std::uint32_t>(image.width));
    PutBe32(header, static_cast<std::uint32_t>(image.height));
    header.insert(header.end(), { 8, 6, 0, 0, 0 });
    Chunk(file, "IHDR", header);
    Chunk(file, "IDAT", zlib);
    Chunk(file, "IEND", {});
    std::FILE * stream = std::fopen(path.string().c_str(), "wb");

    if (stream == nullptr)
    {
        error = "cannot create preview: " + path.string();
        return false;
    }

    const bool written = std::fwrite(file.data(), 1, file.size(), stream) == file.size();

    if (std::fclose(stream) != 0 || !written)
    {
        error = "cannot write preview: " + path.string();
        return false;
    }

    return true;
}

} // namespace assets
