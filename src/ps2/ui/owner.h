// ================================================================================================
// File: owner.h
// Brief: Own the native widgets and dialog state required by checked retail timelines, borrowing their movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

class idMenuHandler;
class idSWF;
class idSWFScriptFunction;
class idSWFSpriteInstance;

namespace ps2::ui
{

// Attach before the shell's first frame executes; detach before its borrowed movie is destroyed.
PS2_COLD_FUNC void AttachOwner(idSWF & movie, unsigned movieIndex);

// The native shell handler owns its widgets, screens and movie. Attach once it installed its
// callbacks and before execution starts; detach before the handler destroys the movie.
PS2_COLD_FUNC void AttachMenu(idMenuHandler & handler);
PS2_COLD_FUNC void DetachMenu(idSWF & movie);

// Publish idCommonDialog's constants and the native quit confirmation before the dialog's first frame.
PS2_COLD_FUNC void ShowQuitDialog(idSWF & movie);
PS2_COLD_FUNC void DetachOwner(idSWF & movie);

// The native shell's logo transition (idMenuHandler_Shell::ShowLogo), inside an execution scope.
void ShowShellLogo(idSWF & movie, bool show);
bool IsOwnerCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie);

// A native key handler: an event wrapper of one of the attached menu handler's live widgets.
bool IsEventHandler(const idSWFScriptFunction * function, const idSWF * movie);

// Common's dialog manager installs each shown dialog's choices as globals; pin them as it does,
// so only those objects answer the movie's calls to them, by name or through copies.
PS2_COLD_FUNC void AttachDialogChoices(idSWF & movie);
void PinShownChoices(idSWF & movie);
PS2_COLD_FUNC void DetachDialogChoices(idSWF & movie);
bool IsDialogChoice(const idSWFScriptFunction * function, const idSWF * movie);
void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing, idSWF * movie);
PS2_COLD_FUNC void OwnerStatus();

} // namespace ps2::ui
