// ================================================================================================
// File: retail_assets.h
// Brief: Own a checked English resource catalog and immutable native font snapshots through UI use.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/retail_catalog.h"

class idFile;

namespace ps2::ui::retail
{
// Opt in while the device/UI are idle, before any authored paged images or fonts are loaded.
// Close fonts/images before Shutdown. General BSWF and Shell loading are not admitted here.
void Init();
void Shutdown();
bool IsInitialized();
const CatalogView & GetCatalog();
bool FindFont(const char * requested, FontRecord & output);
idFile * OpenFont(const FontRecord & font);
void RegisterMaterials();
bool MaterialText(const char * name, char * output, std::size_t capacity);
} // namespace ps2::ui::retail
