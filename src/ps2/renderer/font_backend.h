// ================================================================================================
// File: font_backend.h
// Brief: Own bounded native English fonts while the CPU UI renderer is active.
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
idFile * OpenFontAsset(const char * name);
bool PreflightFont(idFile * file, const char * name);
void ValidateFontMaterial(const idMaterial * material, const char * name);
void GetFontLayout(const char * name, int index, int & width, int & height);
bool PreflightAuthoredFont(idFile * file, const char * name);
void ValidateAuthoredFontMaterial(const idMaterial * material);
} // namespace ps2::fonts
