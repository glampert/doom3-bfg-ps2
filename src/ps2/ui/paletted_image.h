// ================================================================================================
// File: paletted_image.h
// Brief: Draw native UI images from converted GSTX textures: paletted pages and CLUTs stay resident,
// and each triangle samples the placement its UV centroid lands in, clamped to that placement.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/gstx_format.h"
#include "ps2/ui/page_image.h"

namespace ps2::ui
{

// Totals over every open paletted image.
struct PaletteStats
{
    u32 images = 0;
    u32 pages = 0;
    u32 cluts = 0;
    // Decoded index and CLUT texels the texture owner keeps for uploads.
    u32 pixelBytes = 0;
    // Validated tables and the placement lookup grid.
    u32 metadataBytes = 0;
    u32 triangles = 0;
    // Screen blending's per-channel CLUT textures (EnableChannelCluts), outside the counts above.
    u32 channelCluts = 0;
    u32 channelPixelBytes = 0;
};

class PalettedImage final : public PagedImage
{
public:
    PalettedImage() = default;
    ~PalettedImage() override;
    PalettedImage(const PalettedImage &) = delete;
    PalettedImage & operator=(const PalettedImage &) = delete;

    // Admits exactly the file whose length is fileBytes and whose header and tables equal the
    // catalog descriptor, then verifies and loads every CLUT and page. Nothing stays owned on
    // failure. Requires the texture owner; never call during a frame.
    bool Open(const char * path, const std::uint8_t * descriptor, std::uint32_t descriptorBytes, std::uint32_t fileBytes);
    void Close();
    bool IsOpen() const override;
    PageImageStats GetStats() const override;
    const gstx::Header & GetHeader() const;
    // Every CLUT entry with coverage has RGB 255, so one CLUT serves each screen channel.
    bool IsWhite() const;
    bool SupportsScreen() const override;
    // Screen blending carries one color channel per pass in alpha: rereads the file's CLUTs (path as
    // in Open) and keeps three textures whose alpha is a channel times alpha. Never during a frame.
    bool EnableChannelCluts(const char * path);
    // A triangle is valid when its UV centroid lies in a placement and its mapped page UVs clip.
    bool ValidateTriangle(const Vertex (&vertices)[3]) const override;
    // Screen blending needs a white image or channel CLUTs (EnableChannelCluts).
    bool DrawTriangle(const Vertex (&vertices)[3], const renderer::Shading & shading) override;

private:
    bool Load(std::FILE * file, std::uint8_t * scratch, std::uint32_t scratchBytes);
    // Reads every CLUT into its tile of a staging CLUT texture; alpha optionally scaled by one channel.
    bool ReadCluts(std::FILE * file, std::uint8_t * scratch, std::uint32_t scratchBytes, std::uint8_t * texels, int channel);
    bool BuildGrid();
    bool Find(const Vertex (&vertices)[3], gstx::Placement & output) const;
    void Map(const Vertex (&input)[3], const gstx::Placement & placement, Vertex (&output)[3]) const;

    gstx::Header m_header = {};
    std::uint8_t * m_tables = nullptr;
    renderer::tex::Handle m_pages[gstx::kMaxPages] = {};
    renderer::tex::Handle m_cluts;
    renderer::tex::Handle m_channelCluts[3] = {};
    u32 m_channelPixelBytes = 0;
    bool m_white = false;
    // Placement lookup: cell c lists m_cellItems[m_cellStart[c] .. m_cellStart[c + 1]).
    std::uint32_t * m_cellStart = nullptr;
    u16 * m_cellItems = nullptr;
    u32 m_cellSize = 0;
    u32 m_gridColumns = 0;
    u32 m_gridRows = 0;
    PageImageStats m_stats = {};
    bool m_open = false;
};

PaletteStats GetPaletteStats();

} // namespace ps2::ui
