// ================================================================================================
// File: filesystem.cpp
// Brief: Keep path bounds, I/O failures and driver capability limits explicit at the libc boundary,
// supply Doom's directory and save-file contracts over them, and give the core test image's Common
// a filesystem jailed to bounded loose-file fixtures.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(ID_HOST_TEST)
    #include <sbv_patches.h>
#endif // ID_HOST_TEST

namespace ps2::system::fs
{
namespace
{

#if !defined(ID_HOST_TEST)
static bool s_initialized = false;
static bool s_fileIoPatched = false;
#endif // ID_HOST_TEST

bool Components(const char * path, bool allowEmpty, bool allowTrailing)
{
    if (path == nullptr || std::strlen(path) >= kPathCapacity)
    {
        return false;
    }
    if (*path == '\0')
    {
        return allowEmpty;
    }

    const char * start = path;
    for (const char * cursor = path;; ++cursor)
    {
        const char letter = *cursor;
        if (letter == '/' || letter == '\0')
        {
            const size_t length = static_cast<size_t>(cursor - start);
            if (length == 0 || (length == 1 && *start == '.') ||
                (length == 2 && start[0] == '.' && start[1] == '.'))
            {
                return false;
            }
            if (letter == '\0')
            {
                return true;
            }
            start = cursor + 1;
            if (*start == '\0')
            {
                return allowTrailing;
            }
        }
        else if (letter < ' ' || letter > '~' || letter == ':' || letter == '\\')
        {
            return false;
        }
    }
}

const char * PathBody(const char * path)
{
    if (path == nullptr)
    {
        return nullptr;
    }
    if (*path == '/')
    {
        return path + 1;
    }

    const char * colon = std::strchr(path, ':');
    if (colon == nullptr)
    {
        return path;
    }
    if (colon == path || colon - path > 16)
    {
        return nullptr;
    }

    for (const char * cursor = path; cursor != colon; ++cursor)
    {
        const char letter = *cursor;
        if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
              (letter >= '0' && letter <= '9') || letter == '_'))
        {
            return nullptr;
        }
    }

    return colon[1] == '/' ? colon + 2 : colon + 1;
}

char Lower(char letter)
{
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + ('a' - 'A')) : letter;
}

bool HasExtension(const char * name, const char * extension)
{
    if (extension == nullptr || *extension == '\0')
    {
        return true;
    }

    const size_t length = std::strlen(name);
    const size_t suffix = std::strlen(extension);
    if (suffix > length)
    {
        return false;
    }

    for (size_t index = 0; index < suffix; ++index)
    {
        if (Lower(name[length - suffix + index]) != Lower(extension[index]))
        {
            return false;
        }
    }

    return true;
}

PS2_COLD_FUNC bool PathError(char * output, int error)
{
    output[0] = '\0';
    errno = error;
    return false;
}

} // namespace

PS2_COLD_FUNC bool Init()
{
#if defined(ID_HOST_TEST)
    return true;
#else // ID_HOST_TEST
    if (!s_initialized)
    {
        s_initialized = true;

        // FILEIO 1.01 has remove fallthrough and unprotected getstat/dread DMA. Do not reset the IOP.
        s_fileIoPatched = sbv_patch_fileio() == 0;
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] FILEIO patch %s\n", s_fileIoPatched ? "applied" : "unavailable; removal disabled");
    }

    return s_fileIoPatched;
#endif // ID_HOST_TEST
}

bool IsRelativePath(const char * path, bool allowEmpty)
{
    return Components(path, allowEmpty, false);
}

bool IsOSPath(const char * path)
{
    return path != nullptr && (*path == '/' || std::strchr(path, ':') != nullptr);
}

bool IsValidPath(const char * path)
{
    if (path == nullptr || *path == '\0' || std::strlen(path) >= kPathCapacity) [[unlikely]]
    {
        return false;
    }

    const char * body = PathBody(path);
    return body != nullptr && Components(body, IsOSPath(path), true);
}

bool BuildPath(char * output, size_t capacity, const char * base, const char * game, const char * relative)
{
    if (output == nullptr || capacity == 0) [[unlikely]]
    {
        errno = EINVAL;
        return false;
    }

    if (base == nullptr || (*base != '\0' && !IsValidPath(base)) ||
        !IsRelativePath(game, true) || !IsRelativePath(relative, true)) [[unlikely]]
    {
        return PathError(output, EINVAL);
    }

    char path[kPathCapacity] = {};
    size_t length = 0;
    const char * parts[] = { base, game, relative };

    for (const char * part : parts)
    {
        if (*part == '\0')
        {
            continue;
        }

        const size_t size = std::strlen(part);
        const bool separator = length != 0 && path[length - 1] != '/';

        if (length + (separator ? 1u : 0u) + size >= sizeof(path)) [[unlikely]]
        {
            return PathError(output, ENAMETOOLONG);
        }
        if (separator)
        {
            path[length++] = '/';
        }

        std::memcpy(path + length, part, size + 1);
        length += size;
    }

    if (length == 0 || length >= capacity) [[unlikely]]
    {
        return PathError(output, ENAMETOOLONG);
    }

    std::memcpy(output, path, length + 1);
    return true;
}

FILE * Open(const char * path, OpenMode mode)
{
    if (!IsValidPath(path)) [[unlikely]]
    {
        errno = EINVAL;
        return nullptr;
    }

    const char * flags = nullptr;
    switch (mode)
    {
    case OpenMode::Read:
        flags = "rb";
        break;
    case OpenMode::Write:
        flags = "wb+";
        break;
    case OpenMode::Append:
        flags = "ab+";
        break;
    default:
        errno = EINVAL;
        return nullptr;
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
    if (file == nullptr) [[unlikely]]
    {
        errno = EINVAL;
        return -1;
    }

    const long position = std::ftell(file);
    if (position < 0 || std::fseek(file, 0, SEEK_END) != 0)
    {
        return -1;
    }

    const long length = std::ftell(file);
    const int error = errno;

    if (std::fseek(file, position, SEEK_SET) != 0)
    {
        return -1;
    }

    if (length < 0) [[unlikely]]
    {
        errno = error;
        return -1;
    }
    if (length > INT_MAX) [[unlikely]]
    {
        errno = EOVERFLOW;
        return -1;
    }

    return static_cast<int>(length);
}

bool ReadExact(FILE * file, void * output, size_t size)
{
    if (file == nullptr || (output == nullptr && size != 0)) [[unlikely]]
    {
        errno = EINVAL;
        return false;
    }

    return (size == 0 || std::fread(output, 1, size, file) == size) && std::ferror(file) == 0;
}

bool CreateParents(const char * filePath)
{
    if (!IsValidPath(filePath)) [[unlikely]]
    {
        errno = EINVAL;
        return false;
    }

    char path[kPathCapacity];
    std::memcpy(path, filePath, std::strlen(filePath) + 1);
    const size_t start = static_cast<size_t>(PathBody(filePath) - filePath);

    for (size_t index = start; path[index] != '\0'; ++index)
    {
        if (path[index] != '/')
        {
            continue;
        }

        path[index] = '\0';
        const int result = mkdir(path, 0777);
        const int error = errno;
        struct stat status = {};
        const bool exists = result == 0 || (error == EEXIST && stat(path, &status) == 0 && S_ISDIR(status.st_mode));
        path[index] = '/';

        if (!exists)
        {
            errno = error;
            return false;
        }
    }

    return true;
}

bool Remove(const char * path)
{
    if (!IsValidPath(path)) [[unlikely]]
    {
        errno = EINVAL;
        return false;
    }

#if !defined(ID_HOST_TEST)
    // Never invoke ROM removal before its fallthrough fix is installed.
    if (!s_fileIoPatched)
    {
        errno = ENOSYS;
        return false;
    }
#endif // ID_HOST_TEST

    return std::remove(path) == 0;
}

bool Rename(const char * oldPath, const char * newPath)
{
    if (!IsValidPath(oldPath) || !IsValidPath(newPath)) [[unlikely]]
    {
        errno = EINVAL;
        return false;
    }

    // fio has no rename: preserve ENOSYS, rather than pretending copy/delete is atomic.
    return std::rename(oldPath, newPath) == 0;
}

int List(const char * directory, const char * extension, void (*visit)(const char *, void *), void * context)
{
    if (!IsValidPath(directory) || visit == nullptr) [[unlikely]]
    {
        errno = EINVAL;
        return -1;
    }

    DIR * handle = opendir(directory);
    if (handle == nullptr)
    {
        return -1;
    }

    const bool directories = extension != nullptr && std::strcmp(extension, "/") == 0;
    int count = 0;
    int error = 0;

    while (true)
    {
        errno = 0;
        const dirent * entry = readdir(handle);
        if (entry == nullptr)
        {
            error = errno;
            break;
        }

        if (!IsRelativePath(entry->d_name) || std::strchr(entry->d_name, '/') != nullptr)
        {
            continue;
        }

        bool folder = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN)
        {
            char child[kPathCapacity];
            struct stat status = {};
            if (!BuildPath(child, sizeof(child), directory, "", entry->d_name) || lstat(child, &status) != 0)
            {
                error = errno;
                break;
            }

            folder = S_ISDIR(status.st_mode);
            if (!folder && !S_ISREG(status.st_mode))
            {
                continue;
            }
        }
        else if (!folder && entry->d_type != DT_REG)
        {
            continue;
        }
        if (folder != directories || (!directories && !HasExtension(entry->d_name, extension)))
        {
            continue;
        }
        if (count == INT_MAX)
        {
            error = EOVERFLOW;
            break;
        }
        visit(entry->d_name, context);
        ++count;
    }

    const int closed = closedir(handle);
    if (error != 0) [[unlikely]]
    {
        errno = error;
        return -1;
    }

    return closed == 0 ? count : -1;
}

} // namespace ps2::system::fs

// ------------------------------------------------------------------------------------------------
// Doom's contracts over these services
// ------------------------------------------------------------------------------------------------

// The host harness tests the services above without id's headers. idlib comes in here, after the
// libc code, because Str.h redefines libc names such as strcmp as macros. The core image compiles
// idlib's reduced header set, which leaves out the save-game headers.
#if !defined(ID_HOST_TEST)

#include "ps2/system/heap.h"
#include <idlib/precompiled.h>
#include <framework/File_SaveGame.h>
#include <sys/sys_savegame.h>

namespace ps2::system::fs
{
namespace
{

static_assert(MAX_OSPATH == kPathCapacity);

void AppendName(const char * name, void * context)
{
    static_cast<idStrList *>(context)->Append(name);
}

} // namespace
} // namespace ps2::system::fs

namespace fs = ps2::system::fs;

// ------------------------------------------------------------------------------------------------
// Directory contracts
// ------------------------------------------------------------------------------------------------

void Sys_Mkdir(const char * path)
{
    if (!fs::IsValidPath(path))
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
    if (!fs::IsValidPath(path))
    {
        errno = EINVAL;
        return false;
    }
    return rmdir(path) == 0;
}

bool Sys_IsFileWritable(const char * path)
{
    struct stat status = {};
    return fs::IsValidPath(path) && stat(path, &status) == 0 &&
           S_ISREG(status.st_mode) && (status.st_mode & S_IWUSR) != 0;
}

sysFolder_t Sys_IsFolder(const char * path)
{
    if (!fs::IsValidPath(path))
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
    const int count = fs::List(directory, extension, fs::AppendName, &list);
    if (count < 0)
    {
        list.Clear();
    }
    return count;
}

// The current bootstrap has no saved loader argv. Do not invent an executable filename.
const char * Sys_EXEPath()
{
    return ""; // FIXME.
}

const char * Sys_CWD()
{
    static char s_path[fs::kPathCapacity];
    if (getcwd(s_path, sizeof(s_path)) == nullptr)
    {
        return "";
    }
    return s_path;
}

// The loader's HostFs root is the only launch root currently preserved by this bootstrap.
const char * Sys_LaunchPath()
{
    return "host:"; // FIXME: support usb/hdd.
}

// ------------------------------------------------------------------------------------------------
// Save games: native save-description ownership, without storage, compression or retry dialogs
// ------------------------------------------------------------------------------------------------

idCVar saveGame_enable("saveGame_enable", "0", CVAR_BOOL | CVAR_ROM,
                       "save storage is unavailable; changing this value does not enable the save manager");

idSaveGameDetails::idSaveGameDetails()
{
    Clear();
}

void idSaveGameDetails::Clear()
{
    descriptors.Clear();
    damaged = false;
    date = 0;
    slotName.Clear();
}

// Preserve the native spelling and signature; do not claim a retry was queued.
void idSaveGameManager::ShowRetySaveDialog(const char *, const int64)
{
    ps2::FatalError("save capability unavailable: idSaveGameManager::ShowRetySaveDialog");
}

// id's File_SaveGame.cpp streams saves through zlib, which is not linked. Compressed saves return
// with miniz; until then only the members the campaign code names exist, and each one fails.
[[noreturn]] static PS2_COLD_FUNC void SaveFileUnavailable(const char * method)
{
    ps2::FatalError("save capability unavailable: idFile_SaveGamePipelined::%s", method);
}

idFile_SaveGamePipelined::idFile_SaveGamePipelined()
{
    SaveFileUnavailable("idFile_SaveGamePipelined");
}

idFile_SaveGamePipelined::~idFile_SaveGamePipelined()
{
    SaveFileUnavailable("~idFile_SaveGamePipelined");
}

PS2_COLD_FUNC bool idFile_SaveGamePipelined::OpenForReading(idFile *)
{
    SaveFileUnavailable("OpenForReading");
}

PS2_COLD_FUNC bool idFile_SaveGamePipelined::OpenForWriting(idFile *)
{
    SaveFileUnavailable("OpenForWriting");
}

void idFile_SaveGamePipelined::Finish()
{
    SaveFileUnavailable("Finish");
}

int idFile_SaveGamePipelined::Read(void *, int)
{
    SaveFileUnavailable("Read");
}

int idFile_SaveGamePipelined::Write(const void *, int)
{
    SaveFileUnavailable("Write");
}

// ------------------------------------------------------------------------------------------------
// Core test image: Common's idFileSystem over bounded loose-file fixtures
// ------------------------------------------------------------------------------------------------

// Only the core image's objects define ID_PS2_CORE; production and the game images link id's
// idFileSystemLocal, which defines the same global.
#if defined(ID_PS2_CORE) && ID_PS2_CORE

namespace ps2::system::fs
{
namespace
{

static constexpr size_t kFixtureLimit = 65536;

[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * operation)
{
    ps2::FatalError("unsupported headless core operation: %s", operation);
}

// Keep unsupported interfaces visible at the ABI boundary. A core compile/link is not a game boot.
#define CORE_UNSUPPORTED(result, method, parameters) \
    result method parameters override                \
    {                                                \
        Unsupported(#method);                        \
    }

bool FixturePath(const char * input, char * output, size_t capacity)
{
    if (input == nullptr)
    {
        return false;
    }

    const char * relative = idStr::Cmpn(input, "host:", 5) == 0 ? input + 5 : input;
    const size_t length = strlen(relative);

    if (length >= MAX_OSPATH - 5 || !IsRelativePath(relative))
    {
        return false;
    }

#if defined(ID_HOST_TEST)
    const int written = idStr::snPrintf(output, static_cast<int>(capacity), "%s", relative);
#else // ID_HOST_TEST
    const int written = idStr::snPrintf(output, static_cast<int>(capacity), "host:%s", relative);
#endif // ID_HOST_TEST

    return written > 0 && static_cast<size_t>(written) < capacity;
}

class CoreReadFile final : public idFile_Memory
{
public:
    CoreReadFile(const char * path, void * data, int length, ID_TIME_T timestamp)
        : idFile_Memory(path, static_cast<const char *>(data), length), m_data(data), m_timestamp(timestamp)
    {
    }

    ~CoreReadFile() override
    {
        heap::Free(m_data);
    }

    ID_TIME_T Timestamp() const override
    {
        return m_timestamp;
    }

private:
    void * m_data;
    ID_TIME_T m_timestamp;
};

class CoreFileSystem final : public idFileSystem
{
public:
    void Init() override
    {
        m_initialized = true;
    }

    void Shutdown(bool) override
    {
        m_initialized = false;
    }

    bool IsInitialized() const override
    {
        return m_initialized;
    }

    int ReadFile(const char * path, void ** buffer, ID_TIME_T * timestamp) override
    {
        if (buffer != nullptr)
        {
            *buffer = nullptr;
        }
        if (timestamp != nullptr)
        {
            *timestamp = FILE_NOT_FOUND_TIMESTAMP;
        }

        char normalized[MAX_OSPATH] = {};
        if (!m_initialized || !FixturePath(path, normalized, sizeof(normalized)))
        {
            return -1;
        }

        FILE * file = Open(normalized, OpenMode::Read);
        if (file == nullptr)
        {
            return -1;
        }

        const int length = FileLength(file);
        if (length < 0 || (buffer != nullptr && static_cast<size_t>(length) > kFixtureLimit))
        {
            fclose(file);
            return -1;
        }

        const ID_TIME_T fileTime = Sys_FileTimeStamp(file);
        void * data = nullptr;
        if (buffer != nullptr)
        {
            data = heap::TryAlloc(static_cast<size_t>(length) + 1, static_cast<std::uint16_t>(TAG_IDLIB));
            if (data == nullptr)
            {
                fclose(file);
                return -1;
            }
            if (!ReadExact(file, data, static_cast<size_t>(length)))
            {
                heap::Free(data);
                fclose(file);
                return -1;
            }
            static_cast<char *>(data)[static_cast<size_t>(length)] = '\0';
        }

        if (fclose(file) != 0)
        {
            heap::Free(data);
            return -1;
        }

        if (buffer != nullptr)
        {
            *buffer = data;
        }
        if (timestamp != nullptr)
        {
            *timestamp = fileTime;
        }

        return static_cast<int>(length);
    }

    void FreeFile(void * buffer) override
    {
        heap::Free(buffer);
    }

    int GetFileLength(const char * path) override
    {
        return ReadFile(path, nullptr, nullptr);
    }

    idFile * OpenFileRead(const char * path, bool, const char *) override
    {
        char normalized[MAX_OSPATH] = {};
        if (!m_initialized || !FixturePath(path, normalized, sizeof(normalized)))
        {
            return nullptr;
        }
        return idFile_Permanent::OpenPortableRead(path, normalized);
    }

    idFile * OpenFileReadMemory(const char * path, bool, const char *) override
    {
        void * data = nullptr;
        ID_TIME_T timestamp = FILE_NOT_FOUND_TIMESTAMP;
        const int length = ReadFile(path, &data, &timestamp);
        if (length < 0)
        {
            return nullptr;
        }
        return new CoreReadFile(path, data, length, timestamp);
    }

    idFile * OpenExplicitFileRead(const char * path) override
    {
        return OpenFileRead(path, false, nullptr);
    }

    void CloseFile(idFile * file) override
    {
        delete file;
    }

    findFile_t FindFile(const char * path) override
    {
        return GetFileLength(path) >= 0 ? FIND_YES : FIND_NO;
    }

    bool FilenameCompare(const char * first, const char * second) const override
    {
        return idStr::IcmpPath(first, second) != 0;
    }

    bool InProductionMode() override
    {
        return false;
    }

    bool UsingResourceFiles() override
    {
        return false;
    }

    CORE_UNSUPPORTED(void, Restart, ())
    CORE_UNSUPPORTED(idFileList *, ListFiles, (const char *, const char *, bool, bool, const char *))
    CORE_UNSUPPORTED(idFileList *, ListFilesTree, (const char *, const char *, bool, const char *))
    CORE_UNSUPPORTED(void, FreeFileList, (idFileList *))

    // The foundation has one host fixture root. Preserve the same path jail for script diagnostics.
    const char * OSPathToRelativePath(const char * path) override
    {
        char normalized[MAX_OSPATH];
        if (!FixturePath(path, normalized, sizeof(normalized)))
        {
            return "";
        }
        return idStr::Cmpn(path, "host:", 5) == 0 ? path + 5 : path;
    }

    const char * RelativePathToOSPath(const char * path, const char *) override
    {
        if (!FixturePath(path, m_path, sizeof(m_path))) [[unlikely]]
        {
            Unsupported("script source path outside fixture root");
        }
        return m_path;
    }

    CORE_UNSUPPORTED(const char *, BuildOSPath, (const char *, const char *, const char *))
    CORE_UNSUPPORTED(const char *, BuildOSPath, (const char *, const char *))
    CORE_UNSUPPORTED(void, CreateOSPath, (const char *))
    CORE_UNSUPPORTED(int, WriteFile, (const char *, const void *, int, const char *))
    CORE_UNSUPPORTED(void, RemoveFile, (const char *))
    CORE_UNSUPPORTED(bool, RemoveDir, (const char *))
    CORE_UNSUPPORTED(bool, RenameFile, (const char *, const char *, const char *))
    CORE_UNSUPPORTED(idFile *, OpenFileWrite, (const char *, const char *))
    CORE_UNSUPPORTED(idFile *, OpenFileAppend, (const char *, bool, const char *))
    CORE_UNSUPPORTED(idFile *, OpenFileByMode, (const char *, fsMode_t))
    CORE_UNSUPPORTED(idFile *, OpenExplicitFileWrite, (const char *))
    CORE_UNSUPPORTED(idFile_Cached *, OpenExplicitPakFile, (const char *))
    CORE_UNSUPPORTED(void, FindDLL, (const char *, char[MAX_OSPATH]))
    CORE_UNSUPPORTED(void, CopyFile, (const char *, const char *))
    CORE_UNSUPPORTED(sysFolder_t, IsFolder, (const char *, const char *))
    CORE_UNSUPPORTED(void, EnableBackgroundCache, (bool))
    CORE_UNSUPPORTED(void, BeginLevelLoad, (const char *, char *, int))
    CORE_UNSUPPORTED(void, EndLevelLoad, ())
    CORE_UNSUPPORTED(void, UnloadMapResources, (const char *))
    CORE_UNSUPPORTED(void, UnloadResourceContainer, (const char *))
    CORE_UNSUPPORTED(void, StartPreload, (const idStrList &))
    CORE_UNSUPPORTED(void, StopPreload, ())
    CORE_UNSUPPORTED(int, ReadFromBGL, (idFile *, void *, int, int))
    CORE_UNSUPPORTED(bool, IsBinaryModel, (const idStr &) const)
    CORE_UNSUPPORTED(bool, IsSoundSample, (const idStr &) const)
    CORE_UNSUPPORTED(bool, GetResourceCacheEntry, (const char *, idResourceCacheEntry &))
    CORE_UNSUPPORTED(void, FreeResourceBuffer, ())
    CORE_UNSUPPORTED(void, AddImagePreload, (const char *, int, int, int, int))
    CORE_UNSUPPORTED(void, AddSamplePreload, (const char *))
    CORE_UNSUPPORTED(void, AddModelPreload, (const char *))
    CORE_UNSUPPORTED(void, AddAnimPreload, (const char *))
    CORE_UNSUPPORTED(void, AddParticlePreload, (const char *))
    CORE_UNSUPPORTED(void, AddCollisionPreload, (const char *))

private:
    char m_path[MAX_OSPATH] = {};
    bool m_initialized = false;
};

static CoreFileSystem s_coreFileSystem;

#undef CORE_UNSUPPORTED

} // namespace
} // namespace ps2::system::fs

idFileSystem * fileSystem = &ps2::system::fs::s_coreFileSystem;

#endif // ID_PS2_CORE

#endif // ID_HOST_TEST
