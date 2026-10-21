// ================================================================================================
// File: png.h
// Brief: Write RGBA images as PNG files for visual inspection of converted textures, with stored
// deflate blocks so the tools need no compression library.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/image.h"

#include <filesystem>
#include <string>

namespace assets
{

bool WritePng(const std::filesystem::path & path, const RgbaImage & image, std::string & error);

} // namespace assets
