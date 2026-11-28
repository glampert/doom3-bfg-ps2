// ================================================================================================
// File: streamed_file.h
// Brief: A read-only idFile over a file's bytes, read in 32 KiB pieces through
// idFileSystem::ReadFileRange, for loaders that read a large file once from start to end: opening
// a container entry otherwise reads all of it into memory first (the 5.4 MB .bcm was the map
// load's peak).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

class idFile;

namespace ps2::system::fs
{

// The bytes a streamed file keeps of its file at a time.
static constexpr int kStreamedWindowBytes = 32 * 1024;

// The file at 'relativePath' (a container entry or a loose file), or null when it is missing.
// Delete it as any idFile; it reads, seeks and tells, and never writes.
PS2_COLD_FUNC idFile * OpenStreamed(const char * relativePath);

} // namespace ps2::system::fs
