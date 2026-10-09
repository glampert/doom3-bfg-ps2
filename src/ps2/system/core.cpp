// ================================================================================================
// File: core.cpp
// Brief: Real Doom command/CVar/idlib startup with a bounded host fixture filesystem and console.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/core.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>

namespace
{

static constexpr size_t kFixtureLimit = 65536;
static constexpr size_t kPrintCapacity = 2048;

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
    if (length == 0 || length >= MAX_OSPATH - 5 || relative[0] == '/')
    {
        return false;
    }
    const char * component = relative;
    for (const char * cursor = relative;; ++cursor)
    {
        const char letter = *cursor;
        if (letter == '/' || letter == '\0')
        {
            const size_t componentLength = static_cast<size_t>(cursor - component);
            if (componentLength == 0 || (componentLength == 1 && component[0] == '.') ||
                (componentLength == 2 && component[0] == '.' && component[1] == '.'))
            {
                return false;
            }
            if (letter == '\0')
            {
                break;
            }
            component = cursor + 1;
        }
        else if (letter < ' ' || letter > '~' || letter == ':' || letter == '\\')
        {
            return false;
        }
    }
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
        FILE * file = fopen(normalized, "rb");
        if (file == nullptr)
        {
            return -1;
        }
        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            return -1;
        }
        const long length = ftell(file);
        if (length < 0 || static_cast<size_t>(length) > kFixtureLimit || fseek(file, 0, SEEK_SET) != 0)
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
            const size_t read = fread(data, 1, static_cast<size_t>(length), file);
            if (read != static_cast<size_t>(length) || ferror(file) != 0)
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
        void * data = nullptr;
        ID_TIME_T timestamp = FILE_NOT_FOUND_TIMESTAMP;
        const int length = ReadFile(path, &data, &timestamp);
        if (length < 0)
        {
            return nullptr;
        }
        return new CoreReadFile(path, data, length, timestamp);
    }

    idFile * OpenFileReadMemory(const char * path, bool allowCopyFiles, const char * gameDir) override
    {
        return OpenFileRead(path, allowCopyFiles, gameDir);
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

class CoreCommon final : public idCommon
{
public:
    void Init(int argc, const char * const *, const char * commandLine) override
    {
        if (m_everInitialized || argc != 0 || (commandLine != nullptr && commandLine[0] != '\0'))
        {
            Unsupported("repeat/argument-driven core initialization");
        }
        m_everInitialized = true;
        Sys_Init();
        idLib::sys = sys;
        idLib::common = this;
        idLib::cvarSystem = cvarSystem;
        idLib::fileSystem = fileSystem;
        fileSystem->Init();
        idLib::Init();
        cmdSystem->Init();
        cvarSystem->Init();
        idCVar::RegisterStaticVars();
        parallelJobManager->Init();
        m_initialized = true;
    }

    void Shutdown() override
    {
        if (!m_initialized)
        {
            return;
        }
        m_shuttingDown = true;
        parallelJobManager->Shutdown();
        cvarSystem->Shutdown();
        cmdSystem->Shutdown();
        idLib::ShutDown();
        fileSystem->Shutdown(false);
        Sys_Shutdown();
        m_initialized = false;
    }

    bool IsInitialized() const override { return m_initialized; }
    bool IsShuttingDown() const override { return m_shuttingDown; }

    void Printf(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        VPrintf(format, arguments);
        va_end(arguments);
    }

    void VPrintf(const char * format, va_list arguments) override
    {
        if (m_redirectBuffer == nullptr)
        {
            ps2::LogV(ps2::LogLevel::Info, format, arguments);
            return;
        }
        char message[kPrintCapacity] = {};
        idStr::vsnPrintf(message, static_cast<int>(sizeof(message)), format, arguments);
        for (const char * cursor = message; *cursor != '\0'; ++cursor)
        {
            if (m_redirectLength == m_redirectCapacity - 1)
            {
                FlushRedirect();
            }
            m_redirectBuffer[m_redirectLength++] = *cursor;
            m_redirectBuffer[m_redirectLength] = '\0';
        }
    }

    void BeginRedirect(char * buffer, int capacity, void (*flush)(const char *)) override
    {
        if (buffer == nullptr || capacity < 2 || flush == nullptr || m_redirectBuffer != nullptr)
        {
            Unsupported("invalid/nested console redirect");
        }
        m_redirectBuffer = buffer;
        m_redirectCapacity = static_cast<size_t>(capacity);
        m_redirectLength = 0;
        m_redirectFlush = flush;
        buffer[0] = '\0';
    }

    void EndRedirect() override
    {
        if (m_redirectBuffer != nullptr)
        {
            FlushRedirect();
            m_redirectBuffer = nullptr;
            m_redirectFlush = nullptr;
        }
    }

    void DPrintf(const char * format, ...) override
    {
        if (!cvarSystem->IsInitialized() || !cvarSystem->GetCVarBool("developer"))
        {
            return;
        }
        va_list arguments;
        va_start(arguments, format);
        VPrintf(format, arguments);
        va_end(arguments);
    }

    void Warning(const char * format, ...) override
    {
        ++m_warningCount;
        va_list arguments;
        va_start(arguments, format);
        VWarning(format, arguments);
        va_end(arguments);
    }

    void DWarning(const char * format, ...) override
    {
        if (!cvarSystem->IsInitialized() || !cvarSystem->GetCVarBool("developer"))
        {
            return;
        }
        va_list arguments;
        va_start(arguments, format);
        VWarning(format, arguments);
        va_end(arguments);
    }

    void PrintWarnings() override { ps2::Log(ps2::LogLevel::Info, "[D3BFG] WARNINGS count=%u\n", m_warningCount); }
    void ClearWarnings(const char *) override { m_warningCount = 0; }
    void SetRefreshOnPrint(bool refresh) override { if (refresh) { Unsupported("refresh console display"); } }
    void StartupVariable(const char *) override { /* This fixture bootstrap has no command-line arguments. */ }

    [[noreturn]] void Error(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        ps2::FatalErrorV(format, arguments);
    }

    [[noreturn]] void FatalError(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        ps2::FatalErrorV(format, arguments);
    }

    bool IsMultiplayer() override { return false; }
    bool IsServer() override { return false; }
    bool IsClient() override { return false; }
    bool GetConsoleUsed() override { return true; }
    currentGame_t GetCurrentGame() const override { return DOOM3_BFG; }

    CORE_UNSUPPORTED(void, CreateMainMenu, ())
    CORE_UNSUPPORTED(void, Quit, ())
    CORE_UNSUPPORTED(void, Frame, ())
    CORE_UNSUPPORTED(void, UpdateScreen, (bool))
    CORE_UNSUPPORTED(void, UpdateLevelLoadPacifier, ())
    CORE_UNSUPPORTED(const char *, KeysFromBinding, (const char *))
    CORE_UNSUPPORTED(const char *, BindingFromKey, (const char *))
    CORE_UNSUPPORTED(int, ButtonState, (int))
    CORE_UNSUPPORTED(int, KeyState, (int))
    CORE_UNSUPPORTED(int, GetSnapRate, ())
    CORE_UNSUPPORTED(void, NetReceiveReliable, (int, int, idBitMsg &))
    CORE_UNSUPPORTED(void, NetReceiveSnapshot, (idSnapShot &))
    CORE_UNSUPPORTED(void, NetReceiveUsercmds, (int, idBitMsg &))
    CORE_UNSUPPORTED(bool, ProcessEvent, (const sysEvent_t *))
    CORE_UNSUPPORTED(bool, LoadGame, (const char *))
    CORE_UNSUPPORTED(bool, SaveGame, (const char *))
    CORE_UNSUPPORTED(idDemoFile *, ReadDemo, ())
    CORE_UNSUPPORTED(idDemoFile *, WriteDemo, ())
    CORE_UNSUPPORTED(idGame *, Game, ())
    CORE_UNSUPPORTED(idRenderWorld *, RW, ())
    CORE_UNSUPPORTED(idSoundWorld *, SW, ())
    CORE_UNSUPPORTED(idSoundWorld *, MenuSW, ())
    CORE_UNSUPPORTED(idSession *, Session, ())
    CORE_UNSUPPORTED(idCommonDialog &, Dialog, ())
    CORE_UNSUPPORTED(void, OnSaveCompleted, (idSaveLoadParms &))
    CORE_UNSUPPORTED(void, OnLoadCompleted, (idSaveLoadParms &))
    CORE_UNSUPPORTED(void, OnLoadFilesCompleted, (idSaveLoadParms &))
    CORE_UNSUPPORTED(void, OnEnumerationCompleted, (idSaveLoadParms &))
    CORE_UNSUPPORTED(void, OnDeleteCompleted, (idSaveLoadParms &))
    CORE_UNSUPPORTED(void, TriggerScreenWipe, (const char *, bool))
    CORE_UNSUPPORTED(void, OnStartHosting, (idMatchParameters &))
    CORE_UNSUPPORTED(int, GetGameFrame, ())
    CORE_UNSUPPORTED(void, LaunchExternalTitle, (int, int, const lobbyConnectInfo_t * const))
    CORE_UNSUPPORTED(void, InitializeMPMapsModes, ())
    CORE_UNSUPPORTED(const idStrList &, GetModeList, () const)
    CORE_UNSUPPORTED(const idStrList &, GetModeDisplayList, () const)
    CORE_UNSUPPORTED(const idList<mpMap_t> &, GetMapList, () const)
    CORE_UNSUPPORTED(void, ResetPlayerInput, (int))
    CORE_UNSUPPORTED(bool, JapaneseCensorship, () const)
    CORE_UNSUPPORTED(void, QueueShowShell, ())
    CORE_UNSUPPORTED(void, SwitchToGame, (currentGame_t))

private:
    void VWarning(const char * format, va_list arguments)
    {
        if (m_redirectBuffer == nullptr)
        {
            ps2::LogV(ps2::LogLevel::Warning, format, arguments);
            return;
        }
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] WARNING ");
        VPrintf(format, arguments);
        ps2::Log(ps2::LogLevel::Info, "\n");
    }

    void FlushRedirect()
    {
        m_redirectFlush(m_redirectBuffer);
        m_redirectLength = 0;
        m_redirectBuffer[0] = '\0';
    }

    bool m_initialized = false;
    bool m_everInitialized = false;
    bool m_shuttingDown = false;
    unsigned int m_warningCount = 0;
    char * m_redirectBuffer = nullptr;
    size_t m_redirectCapacity = 0;
    size_t m_redirectLength = 0;
    void (*m_redirectFlush)(const char *) = nullptr;
};

static CoreCommon s_common;

#undef CORE_UNSUPPORTED

} // namespace

idCommon * common = &s_common;
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
