// ================================================================================================
// File: filesystem.h
// Brief: Bounded paths and synchronous libc file services shared by campaign code and probes.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace ps2::filesystem
{

static constexpr size_t kPathCapacity = 256;
enum class OpenMode { Read, Write, Append };

// Call after SifInitRpc; preserves the loader's IOP. Removal remains disabled if the ROM patch fails.
bool Init();

bool IsRelativePath(const char * path, bool allowEmpty = false);
bool IsOSPath(const char * path);
bool IsValidPath(const char * path);
// Empty game/relative components are useful for listing a root. Inputs may alias output; failure clears it.
bool BuildPath(char * output, size_t capacity, const char * base, const char * game, const char * relative);
FILE * Open(const char * path, OpenMode mode);
// Preserve the cursor; fail rather than truncating lengths outside the engine's signed range.
int FileLength(FILE * file);
bool ReadExact(FILE * file, void * output, size_t size);
bool CreateParents(const char * filePath);
bool Remove(const char * path);
bool Rename(const char * oldPath, const char * newPath);
// extension "/" selects directories; null/empty selects all regular files. No symlink traversal.
int List(const char * directory, const char * extension, void (*visit)(const char *, void *), void * context);
// Host ZIP times use UTC, two-second resolution, clamped to 1980..2107. EE uses 1980-01-01
// until driver timestamps are validated; missing stat still fails on both platforms.
std::uint32_t PackZipTime(const tm & utc);
bool ZipTime(const char * path, std::uint32_t & packed);

} // namespace ps2::filesystem
