// ================================================================================================
// File: menu.h
// Brief: Open, update and close the native shell menu, whose handler owns the admitted shell movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

class idMenuHandler;
class idSWF;
struct sysEvent_t;

namespace ps2::ui
{

// PS2 pads use the PS3 face-button layout and prompt art.
static constexpr int kMenuPlatform = 1;

// idCommonLocal::CreateMainMenu: the game's shell handler opens the movie, shows the menu and syncs
// with the session. Returns the executing, drawable movie, which the handler owns.
PS2_COLD_FUNC idSWF * OpenMenu();

// Once per Common frame, inside the movie's execution scope and before its timeline advances.
void UpdateMenu();

// Releases the handler's widgets, screens and movie, outside any frame.
PS2_COLD_FUNC void CloseMenu();

// idCommonLocal::MenuEvent for the open menu, inside its movie's execution scope, after an active
// dialog's HandleDialogEvent; false without a menu.
bool ProcessMenuEvent(const sysEvent_t * event);

// Logs the events the open menu received and handled, and the dialog's state.
PS2_COLD_FUNC void MenuInputStatus();

// The open menu's dialog movie, which Common's dialog manager owns, or null.
idSWF * MenuDialog();

// Once per Common frame after the menu's update, outside any scope: the dialog manager's state
// update, then the dialog's timeline on the same clock.
void UpdateMenuDialog(int time);

// Retail movies activate on this clock, the time of the presentation's current movie step once it
// sets one (a fixed test step or real time), so a movie shown mid-run follows its advances.
void SetMovieClock(int time);
void ClearMovieClock();
int MovieClock();

// Called by the native menu handlers: open only the admitted shell, and start or stop its checked
// execution, drawing and key events once the handler's callbacks exist.
PS2_COLD_FUNC idSWF * CreateMenuMovie(const char * name);
void ActivateMenuMovie(idMenuHandler & handler, bool show);

// Called by Common's dialog manager while the menu initializes it: the admitted dialog movie, with
// execution, drawing and events admitted but inactive. Release it before deleting it; pin the
// choices each shown dialog installs.
PS2_COLD_FUNC idSWF * CreateDialogMovie();
PS2_COLD_FUNC void ReleaseDialogMovie(idSWF * movie);
void PinDialogChoices(idSWF & movie);

} // namespace ps2::ui
