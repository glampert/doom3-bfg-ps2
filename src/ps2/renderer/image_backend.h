// ================================================================================================
// File: image_backend.h
// Brief: Own console intrinsics and one lazily generated native ASCII font image.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/texture.h"

class idImage;

namespace ps2::render
{
// The caller owns the device. Shutdown images before shutting down the texture owner/device.
bool InitConsoleImages();
void ShutdownConsoleImages();
bool ConsoleImagesInitialized();
idImage * ConsoleWhiteImage();
idImage * ConsoleCharsetImage();
idImage * AuthoredFontImage();
tex::Handle ConsoleImageTexture(const idImage * image);
} // namespace ps2::render
