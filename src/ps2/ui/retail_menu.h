// ================================================================================================
// File: retail_menu.h
// Brief: Open, update and close the native shell menu, whose handler owns the admitted shell movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idMenuHandler;
class idSWF;

namespace ps2::ui::retail
{
// PS2 pads use the PS3 face-button layout and prompt art.
static constexpr int kMenuPlatform = 1;

// idCommonLocal::CreateMainMenu: the game's shell handler opens the movie, shows the menu and syncs
// with the session. Returns the executing, drawable movie, which the handler owns.
idSWF * OpenMenu();
// Once per Common frame, inside the movie's execution scope and before its timeline advances.
void UpdateMenu();
// Releases the handler's widgets, screens and movie, outside any frame.
void CloseMenu();

// Called by the native menu handlers: open only the admitted shell, and start or stop its checked
// execution and drawing once the handler's callbacks exist.
idSWF * CreateMenuMovie(const char * name);
void ActivateMenuMovie(idMenuHandler & handler, bool show);
} // namespace ps2::ui::retail
