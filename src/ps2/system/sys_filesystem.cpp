// ================================================================================================
// File: sys_filesystem.cpp
// Brief: Supply Doom's directory contracts through the bounded synchronous filesystem services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"

#include <cerrno>
#include <idlib/precompiled.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

static_assert(MAX_OSPATH == ps2::system::fs::kPathCapacity);

void AppendName(const char * name, void * context)
{
    static_cast<idStrList *>(context)->Append(name);
}

} // namespace

void Sys_Mkdir(const char * path)
{
    if (!ps2::system::fs::IsValidPath(path))
    {
        errno = EINVAL;
    }
    else if (mkdir(path, 0777) == 0)
    {
        return;
    }
    else if (errno == EEXIST && Sys_IsFolder(path) == FOLDER_YES)
    {
        return;
    }
    ps2::Log(ps2::LogLevel::Warning, "mkdir(%s) failed: %d", path != nullptr ? path : "(null)", errno);
}

bool Sys_Rmdir(const char * path)
{
    if (!ps2::system::fs::IsValidPath(path))
    {
        errno = EINVAL;
        return false;
    }
    return rmdir(path) == 0;
}

bool Sys_IsFileWritable(const char * path)
{
    struct stat status = {};
    return ps2::system::fs::IsValidPath(path) && stat(path, &status) == 0 &&
           S_ISREG(status.st_mode) && (status.st_mode & S_IWUSR) != 0;
}

sysFolder_t Sys_IsFolder(const char * path)
{
    if (!ps2::system::fs::IsValidPath(path))
    {
        errno = EINVAL;
        return FOLDER_ERROR;
    }
    struct stat status = {};
    if (stat(path, &status) != 0)
    {
        return FOLDER_ERROR;
    }
    return S_ISDIR(status.st_mode) ? FOLDER_YES : FOLDER_NO;
}

int Sys_ListFiles(const char * directory, const char * extension, idStrList & list)
{
    list.Clear();
    const int count = ps2::system::fs::List(directory, extension, AppendName, &list);
    if (count < 0)
    {
        list.Clear();
    }
    return count;
}

// The current bootstrap has no saved loader argv. Do not invent an executable filename.
const char * Sys_EXEPath()
{
    return "";
}

const char * Sys_CWD()
{
    static char s_path[ps2::system::fs::kPathCapacity];
    if (getcwd(s_path, sizeof(s_path)) == nullptr)
    {
        return "";
    }
    return s_path;
}

// The loader's HostFs root is the only launch root currently preserved by this bootstrap.
const char * Sys_LaunchPath()
{
    return "host:";
}
