// ================================================================================================
// File: retail_assets.h
// Brief: Own checked English fonts, localization and movie snapshots through UI use.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/retail_catalog.h"
#include "ps2/ui/swf_format.h"

class idFile;

namespace ps2::ui::retail
{
// Opt in while the device/UI are idle, before any authored paged images or fonts are loaded.
// Close movies, fonts and images before Shutdown. Retail construction is inactive; execution requires separate command/action admission.
void Init();
void Shutdown();
bool IsInitialized();
const CatalogView & GetCatalog();
bool FindFont(const char * requested, FontRecord & output);
idFile * OpenFont(const FontRecord & font);
// The fixed shell/dialog profile loads one movie at a time; execute opts into additional preflight.
// Render additionally requires a movie free of draw features the 2D backend cannot express.
void PrepareMovie(unsigned index, bool execute = false, bool render = false);
bool MovieExecutionAdmitted();
bool MovieRenderAdmitted();
idFile * OpenMovie(const char * logicalPath);
SwfSummary GetMovieSummary();
void RegisterMaterials();
bool MaterialText(const char * name, char * output, std::size_t capacity);
} // namespace ps2::ui::retail
