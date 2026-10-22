// ================================================================================================
// File: console_charset.h
// Brief: Generate the authored 5x7 ASCII charset the renderer tests draw with: the core test
// image has no retail catalog to take the original console font from, and the scenes'
// glyph probes are authored against these bit patterns.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>

namespace ps2::tests
{
static constexpr int kConsoleAtlasSize = 256;
static constexpr int kConsoleCellSize = 16;
static constexpr size_t kConsoleAtlasBytes = 256U * 256U * 4U;

// Top-down straight RGBA8, sixteen rows/columns indexed by the native character byte.
// ASCII is authored 5x7; unsupported bytes use a box, and bytes 10/11 are console cursors.
bool MakeConsoleCharset(void * rgba, size_t capacity);
} // namespace ps2::tests
