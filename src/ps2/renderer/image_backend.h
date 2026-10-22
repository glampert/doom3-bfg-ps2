// ================================================================================================
// File: image_backend.h
// Brief: Own console intrinsics and native UI images borrowing one bounded page cache.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/texture.h"

#include <cstddef>

class idImage;

namespace ps2::ui
{
class PagedImage;
struct PageCacheStats;
} // namespace ps2::ui

namespace ps2::renderer
{
// The caller owns the device. Shutdown images before shutting down the texture owner/device.
bool InitConsoleImages();
void ShutdownConsoleImages();
bool ConsoleImagesInitialized();
idImage * ConsoleWhiteImage();
idImage * ConsoleCharsetImage();
// Production opens the original charset from the retail catalog once that catalog is initialized;
// the core test image, which has no retail data, installs the authored RGBA fixture instead.
void OpenConsoleCharset();
#if PS2_D3BFG_TESTS
bool InstallConsoleCharset(const void * rgba, size_t bytes);
#endif
idImage * AuthoredFontImage();
// Validation admits both direct intrinsics and cooked images. A paged image has logical atlas
// dimensions; it never exposes a single texture handle for the whole atlas.
void ValidateUiImage(const idImage * image);
ui::PagedImage * UiPageImage(const idImage * image);
ui::PageCacheStats GetUiPageStats();
tex::Handle ConsoleImageTexture(const idImage * image);
} // namespace ps2::renderer
