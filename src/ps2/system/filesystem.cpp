// ================================================================================================
// File: filesystem.cpp
// Brief: Keep path bounds, I/O failures and driver capability limits explicit at the libc boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#if !defined(ID_HOST_TEST)
#include <sbv_patches.h>
#endif

namespace ps2::filesystem
{
namespace
{

static constexpr std::uint32_t kZipEpoch = 1u << 21 | 1u << 16;

#if !defined(ID_HOST_TEST)
static bool s_initialized = false;
static bool s_fileIoPatched = false;
#endif

bool Components(const char * path, bool allowEmpty, bool allowTrailing)
{
    if (path == nullptr || std::strlen(path) >= kPathCapacity) { return false; }
    if (*path == '\0') { return allowEmpty; }
    const char * start = path;
    for (const char * cursor = path;; ++cursor)
    {
        const char letter = *cursor;
        if (letter == '/' || letter == '\0')
        {
            const size_t length = static_cast<size_t>(cursor - start);
            if (length == 0 || (length == 1 && *start == '.') ||
                (length == 2 && start[0] == '.' && start[1] == '.')) { return false; }
            if (letter == '\0') { return true; }
            start = cursor + 1;
            if (*start == '\0') { return allowTrailing; }
        }
        else if (letter < ' ' || letter > '~' || letter == ':' || letter == '\\') { return false; }
    }
}

const char * PathBody(const char * path)
{
    if (path == nullptr) { return nullptr; }
    if (*path == '/') { return path + 1; }
    const char * colon = std::strchr(path, ':');
    if (colon == nullptr) { return path; }
    if (colon == path || colon - path > 16) { return nullptr; }
    for (const char * cursor = path; cursor != colon; ++cursor)
    {
        const char letter = *cursor;
        if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') || letter == '_')) { return nullptr; }
    }
    return colon[1] == '/' ? colon + 2 : colon + 1;
}

char Lower(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + ('a' - 'A')) : letter;
}

bool HasExtension(const char * name, const char * extension)
{
    if (extension == nullptr || *extension == '\0') { return true; }
    const size_t length = std::strlen(name);
    const size_t suffix = std::strlen(extension);
    if (suffix > length) { return false; }
    for (size_t index = 0; index < suffix; ++index)
    {
        if (Lower(name[length - suffix + index]) != Lower(extension[index])) { return false; }
    }
    return true;
}

bool PathError(char * output, int error)
{
    output[0] = '\0';
    errno = error;
    return false;
}

} // namespace

bool Init()
{
#if defined(ID_HOST_TEST)
    return true;
#else
    if (!s_initialized)
    {
        s_initialized = true;
        // FILEIO 1.01 has remove fallthrough and unprotected getstat/dread DMA. Do not reset the IOP.
        s_fileIoPatched = sbv_patch_fileio() == 0;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] FILEIO patch %s\n", s_fileIoPatched ? "applied" : "unavailable; removal disabled");
    }
    return s_fileIoPatched;
#endif
}

bool IsRelativePath(const char * path, bool allowEmpty) { return Components(path, allowEmpty, false); }
bool IsOSPath(const char * path) { return path != nullptr && (*path == '/' || std::strchr(path, ':') != nullptr); }

bool IsValidPath(const char * path)
{
    if (path == nullptr || *path == '\0' || std::strlen(path) >= kPathCapacity) { return false; }
    const char * body = PathBody(path);
    return body != nullptr && Components(body, IsOSPath(path), true);
}

bool BuildPath(char * output, size_t capacity, const char * base, const char * game, const char * relative)
{
    if (output == nullptr || capacity == 0) { errno = EINVAL; return false; }
    if (base == nullptr || (*base != '\0' && !IsValidPath(base)) ||
        !IsRelativePath(game, true) || !IsRelativePath(relative, true)) { return PathError(output, EINVAL); }
    char path[kPathCapacity] = {};
    size_t length = 0;
    const char * parts[] = {base, game, relative};
    for (const char * part : parts)
    {
        if (*part == '\0') { continue; }
        const size_t size = std::strlen(part);
        const bool separator = length != 0 && path[length - 1] != '/';
        if (length + (separator ? 1u : 0u) + size >= sizeof(path)) { return PathError(output, ENAMETOOLONG); }
        if (separator) { path[length++] = '/'; }
        std::memcpy(path + length, part, size + 1);
        length += size;
    }
    if (length == 0 || length >= capacity) { return PathError(output, ENAMETOOLONG); }
    std::memcpy(output, path, length + 1);
    return true;
}

FILE * Open(const char * path, OpenMode mode)
{
    if (!IsValidPath(path)) { errno = EINVAL; return nullptr; }
    const char * flags = nullptr;
    switch (mode)
    {
    case OpenMode::Read: flags = "rb"; break;
    case OpenMode::Write: flags = "wb+"; break;
    case OpenMode::Append: flags = "ab+"; break;
    default: errno = EINVAL; return nullptr;
    }
    FILE * file = std::fopen(path, flags);
    if (file != nullptr && mode == OpenMode::Append && std::fseek(file, 0, SEEK_END) != 0)
    {
        const int error = errno;
        std::fclose(file);
        errno = error;
        return nullptr;
    }
    return file;
}

int FileLength(FILE * file)
{
    if (file == nullptr) { errno = EINVAL; return -1; }
    const long position = std::ftell(file);
    if (position < 0 || std::fseek(file, 0, SEEK_END) != 0) { return -1; }
    const long length = std::ftell(file);
    const int error = errno;
    if (std::fseek(file, position, SEEK_SET) != 0) { return -1; }
    if (length < 0) { errno = error; return -1; }
    if (length > INT_MAX) { errno = EOVERFLOW; return -1; }
    return static_cast<int>(length);
}

bool ReadExact(FILE * file, void * output, size_t size)
{
    if (file == nullptr || (output == nullptr && size != 0)) { errno = EINVAL; return false; }
    return (size == 0 || std::fread(output, 1, size, file) == size) && std::ferror(file) == 0;
}

bool CreateParents(const char * filePath)
{
    if (!IsValidPath(filePath)) { errno = EINVAL; return false; }
    char path[kPathCapacity];
    std::memcpy(path, filePath, std::strlen(filePath) + 1);
    const size_t start = static_cast<size_t>(PathBody(filePath) - filePath);
    for (size_t index = start; path[index] != '\0'; ++index)
    {
        if (path[index] != '/') { continue; }
        path[index] = '\0';
        const int result = mkdir(path, 0777);
        const int error = errno;
        struct stat status = {};
        const bool exists = result == 0 || (error == EEXIST && stat(path, &status) == 0 && S_ISDIR(status.st_mode));
        path[index] = '/';
        if (!exists) { errno = error; return false; }
    }
    return true;
}

bool Remove(const char * path)
{
    if (!IsValidPath(path)) { errno = EINVAL; return false; }
#if !defined(ID_HOST_TEST)
    // Never invoke ROM removal before its fallthrough fix is installed.
    if (!s_fileIoPatched) { errno = ENOSYS; return false; }
#endif
    return std::remove(path) == 0;
}

bool Rename(const char * oldPath, const char * newPath)
{
    if (!IsValidPath(oldPath) || !IsValidPath(newPath)) { errno = EINVAL; return false; }
    // fio has no rename: preserve ENOSYS, rather than pretending copy/delete is atomic.
    return std::rename(oldPath, newPath) == 0;
}

int List(const char * directory, const char * extension, void (*visit)(const char *, void *), void * context)
{
    if (!IsValidPath(directory) || visit == nullptr) { errno = EINVAL; return -1; }
    DIR * handle = opendir(directory);
    if (handle == nullptr) { return -1; }
    const bool directories = extension != nullptr && std::strcmp(extension, "/") == 0;
    int count = 0;
    int error = 0;
    while (true)
    {
        errno = 0;
        const dirent * entry = readdir(handle);
        if (entry == nullptr) { error = errno; break; }
        if (!IsRelativePath(entry->d_name) || std::strchr(entry->d_name, '/') != nullptr) { continue; }
        bool folder = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN)
        {
            char child[kPathCapacity];
            struct stat status = {};
            if (!BuildPath(child, sizeof(child), directory, "", entry->d_name) || lstat(child, &status) != 0)
            { error = errno; break; }
            folder = S_ISDIR(status.st_mode);
            if (!folder && !S_ISREG(status.st_mode)) { continue; }
        }
        else if (!folder && entry->d_type != DT_REG) { continue; }
        if (folder != directories || (!directories && !HasExtension(entry->d_name, extension))) { continue; }
        if (count == INT_MAX) { error = EOVERFLOW; break; }
        visit(entry->d_name, context);
        ++count;
    }
    const int closed = closedir(handle);
    if (error != 0) { errno = error; return -1; }
    return closed == 0 ? count : -1;
}

std::uint32_t PackZipTime(const tm & utc)
{
    if (utc.tm_year < 80) { return kZipEpoch; }
    if (utc.tm_year > 207) { return 127u << 25 | 12u << 21 | 31u << 16 | 23u << 11 | 59u << 5 | 29u; }
    return static_cast<std::uint32_t>(utc.tm_year - 80) << 25 |
        static_cast<std::uint32_t>(utc.tm_mon + 1) << 21 | static_cast<std::uint32_t>(utc.tm_mday) << 16 |
        static_cast<std::uint32_t>(utc.tm_hour) << 11 | static_cast<std::uint32_t>(utc.tm_min) << 5 |
        static_cast<std::uint32_t>(utc.tm_sec / 2);
}

bool ZipTime(const char * path, std::uint32_t & packed)
{
    packed = 0;
    if (!IsValidPath(path)) { errno = EINVAL; return false; }
    struct stat status = {};
    if (stat(path, &status) != 0) { return false; }
#if defined(ID_HOST_TEST)
    tm utc = {};
    if (gmtime_r(&status.st_mtime, &utc) == nullptr) { return false; }
    packed = PackZipTime(utc);
#else
    // fio dates are unzoned; PCSX2 and libcglue disagree on their year encoding. Do not publish a false date.
    packed = kZipEpoch;
#endif
    return true;
}

} // namespace ps2::filesystem
