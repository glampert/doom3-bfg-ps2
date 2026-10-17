// ================================================================================================
// File: retail_owner.h
// Brief: Own the native widgets required by checked retail Shell timelines while borrowing their movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idSWF;
class idSWFScriptFunction;
class idSWFSpriteInstance;

namespace ps2::ui::retail_owner
{
// Attach before the shell's first frame executes; detach before its borrowed movie is destroyed.
void Init(idSWF & movie, unsigned movieIndex);
void Shutdown(idSWF & movie);
bool IsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie);
void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing, idSWF * movie);
void Status();
} // namespace ps2::ui::retail_owner
