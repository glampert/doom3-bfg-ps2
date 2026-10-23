// ================================================================================================
// File: retail_owner.h
// Brief: Own the native widgets and dialog state required by checked retail timelines, borrowing their movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idMenuHandler;
class idSWF;
class idSWFScriptFunction;
class idSWFSpriteInstance;

namespace ps2::ui::retail
{
// Attach before the shell's first frame executes; detach before its borrowed movie is destroyed.
void AttachOwner(idSWF & movie, unsigned movieIndex);
// The native shell handler owns its widgets, screens and movie. Attach once it installed its
// callbacks and before execution starts; detach before the handler destroys the movie.
void AttachMenu(idMenuHandler & handler);
void DetachMenu(idSWF & movie);
// Publish idCommonDialog's constants and the native quit confirmation before the dialog's first frame.
void ShowQuitDialog(idSWF & movie);
void DetachOwner(idSWF & movie);
// The native shell's logo transition (idMenuHandler_Shell::ShowLogo), inside an execution scope.
void ShowShellLogo(idSWF & movie, bool show);
bool IsOwnerCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie);
void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing, idSWF * movie);
void OwnerStatus();
} // namespace ps2::ui::retail
