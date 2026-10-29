// ================================================================================================
// File: image_backend.h
// Brief: Own console intrinsics and the retail UI's resident paletted images.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/renderer/texture.h"
#include <cstddef>

class idImage;

namespace ps2::ui
{
class PagedImage;
} // namespace ps2::ui

namespace ps2::renderer
{

// The caller owns the device. Shutdown images before shutting down the texture owner/device.
PS2_COLD_FUNC bool InitConsoleImages();
PS2_COLD_FUNC void ShutdownConsoleImages();
bool ConsoleImagesInitialized();
idImage * ConsoleWhiteImage();
idImage * ConsoleCharsetImage();
tex::Handle ConsoleImageTexture(const idImage * image);

// Production opens the original charset from the retail catalog once that catalog is initialized;
// the core test image, which has no retail data, installs the generated RGBA fixture instead.
void OpenConsoleCharset();

#if PS2_D3BFG_TESTS
PS2_COLD_FUNC bool InstallConsoleCharset(const void * rgba, size_t bytes);
#endif // PS2_D3BFG_TESTS

// Validation admits both direct intrinsics and cooked images. A paged image has logical atlas
// dimensions; it never exposes a single texture handle for the whole atlas.
void ValidateUiImage(const idImage * image);
ui::PagedImage * UiPageImage(const idImage * image);

// Screen blending carries one color channel per pass in alpha: gives a loaded retail image the
// channel CLUTs it needs. Outside a frame; white images need none.
void EnableScreenBlending(const idImage * image);

} // namespace ps2::renderer
