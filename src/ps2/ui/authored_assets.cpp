// ================================================================================================
// File: authored_assets.cpp
// Brief: Snapshot and compare authored UI resources before trusting native counts, strings or tags.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/authored_assets.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/authored_ui_data.h"
#include <idlib/precompiled.h>

namespace ps2::ui::authored
{
PS2_COLD_FUNC idFile * OpenAsset(const char * path)
{
    const Resource * resource = nullptr;
    for (const auto & candidate : kResources)
    {
        if (path != nullptr && strcmp(path, candidate.path) == 0)
        {
            resource = &candidate;
            break;
        }
    }
    if (resource == nullptr || resource->length == 0 || resource->length > 16384)
    {
        FatalError("UI asset outside authored profile: %s", path != nullptr ? path : "<null>");
    }
    idFile * const source = fileSystem->OpenFileRead(path);
    if (source == nullptr)
    {
        FatalError("authored UI asset missing: %s", path);
    }
    if (source->Length() != static_cast<int>(resource->length))
    {
        delete source;
        FatalError("authored UI asset length mismatch: %s", path);
    }
    auto * const bytes = static_cast<char *>(system::heap::Alloc(resource->length, TAG_SWF));
    const bool read = source->Read(bytes, static_cast<int>(resource->length)) == static_cast<int>(resource->length);
    delete source;
    if (!read || memcmp(bytes, resource->bytes, resource->length) != 0)
    {
        system::heap::Free(bytes);
        FatalError("authored UI asset content mismatch: %s", path);
    }
    // The parser sees the single checked snapshot, never a second mutable HostFs open.
    auto * const snapshot = new (TAG_SWF) idFile_Memory(path, static_cast<const char *>(bytes), static_cast<int>(resource->length));
    snapshot->TakeDataOwnership();
    return snapshot;
}
} // namespace ps2::ui::authored
