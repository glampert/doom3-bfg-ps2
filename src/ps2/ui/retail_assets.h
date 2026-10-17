// ================================================================================================
// File: retail_assets.h
// Brief: Own checked English fonts, localization and inactive movie snapshots through UI use.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/retail_catalog.h"
#include "ps2/ui/swf_format.h"

class idFile;

namespace ps2::ui::retail
{
// Opt in while the device/UI are idle, before any authored paged images or fonts are loaded.
// Close movies, fonts and images before Shutdown. Retail movies are admitted only as inactive resources.
void Init();
void Shutdown();
bool IsInitialized();
const CatalogView & GetCatalog();
bool FindFont(const char * requested, FontRecord & output);
idFile * OpenFont(const FontRecord & font);
// The staging profile has fixed shell/dialog identities; load one at a time without timeline execution.
void PrepareMovie(unsigned index);
idFile * OpenMovie(const char * logicalPath);
SwfSummary GetMovieSummary();
void RegisterMaterials();
bool MaterialText(const char * name, char * output, std::size_t capacity);
} // namespace ps2::ui::retail
