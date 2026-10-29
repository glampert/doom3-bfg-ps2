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

// The fixed profile's movies are the shell (0) and the dialog (1); each preparation admits one
// construction, and both can be resident at once. Execute opts into additional preflight. Render
// additionally requires a movie free of draw features the 2D backend cannot express.
void PrepareMovie(unsigned index, bool execute = false, bool render = false);

// The profile index of a native idSWF name ("swf/shell.swf"), or -1.
int MovieIndex(const char * name);
bool MovieExecutionAdmitted(int index);
bool MovieRenderAdmitted(int index);

// A drawn movie with screen placements: its atlas needs channel CLUTs before the first frame.
bool MovieUsesScreenBlending(int index);
idFile * OpenMovie(const char * logicalPath);
swf::Summary GetMovieSummary(int index);

void RegisterMaterials();
bool MaterialText(const char * name, char * output, std::size_t capacity);

} // namespace ps2::ui::retail
