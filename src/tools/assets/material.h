// ================================================================================================
// File: material.h
// Brief: Scan retail .mtr declarations for what the texture cook needs from each material, its
// diffuse image and whether that stage alpha-tests, and name the BIMAGE files id generated for an
// image, so the cook reads exactly the pixels the engine would load.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <map>
#include <string>
#include <string_view>

namespace assets
{

// textureUsage_t (Image.h): the two digits after "#__" in a generated image name.
static constexpr int kTextureUsageDiffuse = 1;
static constexpr int kTextureUsageDefault = 2;
static constexpr int kTextureUsageCoverage = 10;

struct MaterialInfo
{
    // The first diffuse stage's image as the material names it ('diffusemap X' or a stage with
    // 'blend diffusemap' and 'map X'), image programs included; empty without one.
    std::string diffuse;
    // That stage alpha-tests: id loads its coverage image too (Material.cpp's coverage stage).
    bool alphaTest = false;
    // Stages that are neither diffuse, bump nor specular (blend, add, filter and the like).
    int otherStages = 0;
};

// Adds the materials of one .mtr file to 'materials', keyed by lowercase name; a name already
// present keeps its first definition. Tables, skins and the other declaration types are skipped.
// This scans the stages the cook needs; it is not id's material parser.
bool ScanMaterials(std::string_view text, std::map<std::string, MaterialInfo> & materials, std::string & error);

// The resource id reads for an image loaded with 'usage' (idImage::GetGeneratedName, then
// idBinaryImage::GetGeneratedFileName): "generated/images/<name>#__UU00.bimage", the name lowercased
// without its extension, '(' and ',' as '/', ')' and spaces dropped.
std::string GeneratedImageName(const std::string & image, int usage);

} // namespace assets
