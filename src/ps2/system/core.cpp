// ================================================================================================
// File: core.cpp
// Brief: Bind the Common foundation to bounded loose-file fixtures and tagged memory reports.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/core.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>

namespace
{

static constexpr size_t kFixtureLimit = 65536;

static constexpr const char * kTagNames[] = {
#define MEM_TAG(x) #x,
#include <idlib/sys/sys_alloc_tags.h>
};
static_assert(ps2::ArrayLength(kTagNames) == TAG_NUM_TAGS);

[[noreturn]] void Unsupported(const char * operation)
{
    ps2::FatalError("unsupported headless core operation: %s", operation);
}

// Keep unsupported interfaces visible at the ABI boundary. A core compile/link is not a game boot.
#define CORE_UNSUPPORTED(result, method, parameters) \
    result method parameters override { Unsupported(#method); }

bool FixturePath(const char * input, char * output, size_t capacity)
{
    if (input == nullptr)
    {
        return false;
    }
    const char * relative = idStr::Cmpn(input, "host:", 5) == 0 ? input + 5 : input;
    const size_t length = strlen(relative);
    if (length >= MAX_OSPATH - 5 || !ps2::filesystem::IsRelativePath(relative)) { return false; }
#if defined(ID_HOST_TEST)
    const int written = idStr::snPrintf(output, static_cast<int>(capacity), "%s", relative);
#else
    const int written = idStr::snPrintf(output, static_cast<int>(capacity), "host:%s", relative);
#endif
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
        ps2::heap::Free(m_data);
    }

    ID_TIME_T Timestamp() const override { return m_timestamp; }

private:
    void * m_data;
    ID_TIME_T m_timestamp;
};

class CoreFileSystem final : public idFileSystem
{
public:
    void Init() override { m_initialized = true; }
    void Shutdown(bool) override { m_initialized = false; }
    bool IsInitialized() const override { return m_initialized; }

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
        FILE * file = ps2::filesystem::Open(normalized, ps2::filesystem::OpenMode::Read);
        if (file == nullptr)
        {
            return -1;
        }
        const int length = ps2::filesystem::FileLength(file);
        if (length < 0 || (buffer != nullptr && static_cast<size_t>(length) > kFixtureLimit))
        {
            fclose(file);
            return -1;
        }
        const ID_TIME_T fileTime = Sys_FileTimeStamp(file);
        void * data = nullptr;
        if (buffer != nullptr)
        {
            data = ps2::heap::TryAlloc(static_cast<size_t>(length) + 1, static_cast<std::uint16_t>(TAG_IDLIB));
            if (data == nullptr)
            {
                fclose(file);
                return -1;
            }
            if (!ps2::filesystem::ReadExact(file, data, static_cast<size_t>(length)))
            {
                ps2::heap::Free(data);
                fclose(file);
                return -1;
            }
            static_cast<char *>(data)[static_cast<size_t>(length)] = '\0';
        }
        if (fclose(file) != 0)
        {
            ps2::heap::Free(data);
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

    void FreeFile(void * buffer) override { ps2::heap::Free(buffer); }

    int GetFileLength(const char * path) override { return ReadFile(path, nullptr, nullptr); }

    idFile * OpenFileRead(const char * path, bool, const char *) override
    {
        char normalized[MAX_OSPATH] = {};
        if (!m_initialized || !FixturePath(path, normalized, sizeof(normalized))) { return nullptr; }
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

    void CloseFile(idFile * file) override { delete file; }

    findFile_t FindFile(const char * path) override
    {
        return GetFileLength(path) >= 0 ? FIND_YES : FIND_NO;
    }

    bool FilenameCompare(const char * first, const char * second) const override
    {
        return idStr::IcmpPath(first, second) != 0;
    }

    bool InProductionMode() override { return false; }
    bool UsingResourceFiles() override { return false; }

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
        if (!FixturePath(path, m_path, sizeof(m_path)))
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

static CoreFileSystem s_fileSystem;

#undef CORE_UNSUPPORTED

} // namespace

idFileSystem * fileSystem = &s_fileSystem;

namespace ps2::core
{

void Init() { common->Init(0, nullptr, nullptr); }
void Shutdown() { common->Shutdown(); }

void PrintMemory(const char * stage)
{
    const heap::Stats totals = heap::GetTotalStats();
    const heap::ArenaStats arena = heap::GetArenaStats();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] MEMORY %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu peak_backing=%zu\n",
        stage, totals.requestedBytes, totals.backingBytes, totals.allocationCount,
        totals.peakRequestedBytes, totals.peakBackingBytes);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] ARENA %s available=%d committed=%zu used=%zu free=%zu peak_committed=%zu\n",
        stage, arena.available ? 1 : 0, arena.committedBytes, arena.usedBytes, arena.freeBytes,
        arena.peakCommittedBytes);
    for (std::uint16_t tag = 0; tag < static_cast<std::uint16_t>(TAG_NUM_TAGS); ++tag)
    {
        const heap::Stats stats = heap::GetStats(tag);
        if (stats.allocationCount != 0 || stats.peakRequestedBytes != 0)
        {
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] TAG %s %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu\n",
                stage, kTagNames[tag], stats.requestedBytes, stats.backingBytes,
                stats.allocationCount, stats.peakRequestedBytes);
        }
    }
}

} // namespace ps2::core
