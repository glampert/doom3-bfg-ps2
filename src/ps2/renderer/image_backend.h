// ================================================================================================
// File: image_backend.h
// Brief: Activate only the two owned native images needed by the authored console profile.
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
tex::Handle ConsoleImageTexture(const idImage * image);
} // namespace ps2::render
