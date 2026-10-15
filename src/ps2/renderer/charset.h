// ================================================================================================
// File: charset.h
// Brief: Generate an authored ASCII console atlas without a retail asset or font dependency.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>

namespace ps2::render
{
static constexpr int kConsoleAtlasSize = 256;
static constexpr int kConsoleCellSize = 16;
static constexpr size_t kConsoleAtlasBytes = 256U * 256U * 4U;
static constexpr int kAuthoredFontAtlasWidth = 512;
static constexpr int kAuthoredFontAtlasHeight = 128;
static constexpr size_t kAuthoredFontAtlasBytes = 512U * 128U * 4U;

// Top-down straight RGBA8, sixteen rows/columns indexed by the native character byte.
// ASCII is authored 5x7; unsupported bytes use a box, and bytes 10/11 are console cursors.
bool MakeConsoleCharset(void * rgba, size_t capacity);
// The same printable coverage uses wider padded cells for native idFont's half-texel UV expansion.
bool MakeAuthoredFontCharset(void * rgba, size_t capacity);
} // namespace ps2::render
