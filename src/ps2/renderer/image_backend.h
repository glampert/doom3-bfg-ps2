// ================================================================================================
// File: image_backend.h
// Brief: Own console intrinsics and native UI images borrowing one bounded page cache.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/texture.h"

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
idImage * AuthoredFontImage();
// Validation admits both direct intrinsics and cooked images. A paged image has logical atlas
// dimensions; it never exposes a single texture handle for the whole atlas.
void ValidateUiImage(const idImage * image);
ui::PagedImage * UiPageImage(const idImage * image);
ui::PageCacheStats GetUiPageStats();
tex::Handle ConsoleImageTexture(const idImage * image);
} // namespace ps2::renderer
