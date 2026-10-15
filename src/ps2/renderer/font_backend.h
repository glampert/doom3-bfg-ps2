// ================================================================================================
// File: font_backend.h
// Brief: Own one bounded authored native font while the CPU UI renderer is active.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idFile;
class idMaterial;

namespace ps2::fonts
{
// Fonts borrow UI images/material declarations; release them before either provider shuts down.
void Init();
void Shutdown();
bool IsInitialized();
// Validate the native on-disk profile before its loader trusts counts, records or material names.
bool PreflightAuthoredFont(idFile * file, const char * name);
void ValidateAuthoredFontMaterial(const idMaterial * material);
} // namespace ps2::fonts
