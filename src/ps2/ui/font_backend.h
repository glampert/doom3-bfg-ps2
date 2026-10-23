// ================================================================================================
// File: font_backend.h
// Brief: Own bounded native English fonts while the CPU UI renderer is active.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

class idFile;
class idFont;
class idMaterial;

namespace ps2::ui::fonts
{
// Fonts borrow UI images/material declarations; release them before either provider shuts down.
void Init();
void Shutdown();
bool IsInitialized();
// Validate the native on-disk profile before its loader trusts counts, records or material names.
idFile * OpenFontAsset(const char * name);
bool PreflightFont(idFile * file, const char * name);
void ValidateFontMaterial(const idMaterial * material, const char * name);
void GetFontLayout(const char * name, int index, int & width, int & height);
bool PreflightAuthoredFont(idFile * file, const char * name);
void ValidateAuthoredFontMaterial(const idMaterial * material);

// One glyph of a font's pre-rendered bitmaps: exact pixels at one size, with its outline beside it
// when the size has one, and its ink origin (left edge, baseline) in 1/16 GS pixels. The bitmap is
// a rectangle of the glyph set image's texels; zero width draws nothing.
struct PrerenderedGlyph
{
    std::uint32_t font = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    bool outline = false;
    std::int16_t originX = 0;
    std::int16_t originY = 0;
};

// Finds `codepoint` among the font's pre-rendered sizes at the glyph's virtual-screen scale (units
// per atlas texel) and outline weight (zero without an outline). Each retail size without bitmaps
// is logged once, so the screens that draw it can have it cooked.
bool FindPrerenderedGlyph(const idFont * font, float scaleX, float scaleY, float strokeWeight, std::uint32_t codepoint,
                          PrerenderedGlyph & output);
// Draws the glyph or its outline in the current color, with the tint and blend of the font's own
// material, its ink origin at the virtual-screen point rounded to whole GS pixels.
void DrawPrerenderedGlyph(const PrerenderedGlyph & glyph, const idMaterial * fontMaterial, float originX, float originY, bool outline);
} // namespace ps2::ui::fonts
