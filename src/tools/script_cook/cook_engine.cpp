// ================================================================================================
// File: cook_engine.cpp
// Brief: The engine services id's lexer, parser and script compiler call in the host script cook:
// console output, fatal errors and read-only files served from the retail containers. Every other
// idCommon/idFileSystem/idFile method is a stub that stops the cook, so a new caller shows up.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The standard headers come first: idlib's precompiled.h redefines printf-family names.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tools/script_cook/cook_engine.h"

#include "idlib/precompiled.h"

// The stubs spell the interfaces' signatures as the engine headers do, names included.
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace
{

// ------------------------------------------------------------------------------------------------
// Diagnostics
// ------------------------------------------------------------------------------------------------

[[noreturn]] void Unsupported(const char * name)
{
    std::fprintf(stderr, "script_cook: the cook does not provide %s\n", name);
    std::exit(2);
}

void PrintV(FILE * stream, const char * prefix, const char * format, va_list arguments)
{
    char text[4096];
    idStr::vsnPrintf(text, sizeof(text), format, arguments);
    std::fprintf(stream, "%s%s", prefix, text);
}

[[noreturn]] void FailV(const char * format, va_list arguments)
{
    PrintV(stderr, "script_cook: ", format, arguments);
    std::fprintf(stderr, "\n");
    std::exit(1);
}

static bool s_verbose = false;

// ------------------------------------------------------------------------------------------------
// Files
// ------------------------------------------------------------------------------------------------

// A whole source held in memory: the lexer reads its length, then all of it.
class CookFile final : public idFile
{
public:
    CookFile(const char * name, std::vector<std::uint8_t> data) : m_name(name), m_data(std::move(data))
    {
    }

    const char * GetName() const override
    {
        return m_name.c_str();
    }

    const char * GetFullPath() const override
    {
        return m_name.c_str();
    }

    int Read(void * buffer, int length) override
    {
        const size_t left = m_data.size() - m_position;
        const size_t count = length < 0 ? 0 : (static_cast<size_t>(length) < left ? static_cast<size_t>(length) : left);
        std::memcpy(buffer, m_data.data() + m_position, count);
        m_position += count;
        return static_cast<int>(count);
    }

    int Length() const override
    {
        return static_cast<int>(m_data.size());
    }

    ID_TIME_T Timestamp() const override
    {
        return 0;
    }

    int Tell() const override
    {
        return static_cast<int>(m_position);
    }

private:
    std::string m_name;
    std::vector<std::uint8_t> m_data;
    size_t m_position = 0;
};

class CookCommon final : public idCommon
{
public:
    void Printf(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        PrintV(stdout, "", format, arguments);
        va_end(arguments);
    }

    void VPrintf(const char * format, va_list arguments) override
    {
        PrintV(stdout, "", format, arguments);
    }

    void DPrintf(const char * format, ...) override
    {
        if (!s_verbose)
        {
            return;
        }

        va_list arguments;
        va_start(arguments, format);
        PrintV(stdout, "", format, arguments);
        va_end(arguments);
    }

    void Warning(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        PrintV(stderr, "script_cook: warning: ", format, arguments);
        va_end(arguments);
        std::fprintf(stderr, "\n");
    }

    void DWarning(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        PrintV(stderr, "script_cook: warning: ", format, arguments);
        va_end(arguments);
        std::fprintf(stderr, "\n");
    }

    void Error(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        FailV(format, arguments);
    }

    void FatalError(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        FailV(format, arguments);
    }

    void Init( int argc, const char * const * argv, const char *cmdline ) override
    {
        Unsupported("Init");
    }

    void Shutdown() override
    {
        Unsupported("Shutdown");
    }

    bool IsShuttingDown() const override
    {
        Unsupported("IsShuttingDown");
    }

    void CreateMainMenu() override
    {
        Unsupported("CreateMainMenu");
    }

    void Quit() override
    {
        Unsupported("Quit");
    }

    bool IsInitialized() const override
    {
        Unsupported("IsInitialized");
    }

    void Frame() override
    {
        Unsupported("Frame");
    }

    void UpdateScreen( bool captureToImage ) override
    {
        Unsupported("UpdateScreen");
    }

    void UpdateLevelLoadPacifier() override
    {
        Unsupported("UpdateLevelLoadPacifier");
    }

    void StartupVariable( const char * match ) override
    {
        Unsupported("StartupVariable");
    }

    void BeginRedirect( char *buffer, int buffersize, void (*flush)( const char * ) ) override
    {
        Unsupported("BeginRedirect");
    }

    void EndRedirect() override
    {
        Unsupported("EndRedirect");
    }

    void SetRefreshOnPrint( bool set ) override
    {
        Unsupported("SetRefreshOnPrint");
    }

    void PrintWarnings() override
    {
        Unsupported("PrintWarnings");
    }

    void ClearWarnings( const char *reason ) override
    {
        Unsupported("ClearWarnings");
    }

    const char * KeysFromBinding( const char *bind ) override
    {
        Unsupported("KeysFromBinding");
    }

    const char * BindingFromKey( const char *key ) override
    {
        Unsupported("BindingFromKey");
    }

    int ButtonState( int key ) override
    {
        Unsupported("ButtonState");
    }

    int KeyState( int key ) override
    {
        Unsupported("KeyState");
    }

    bool IsMultiplayer() override
    {
        Unsupported("IsMultiplayer");
    }

    bool IsServer() override
    {
        Unsupported("IsServer");
    }

    bool IsClient() override
    {
        Unsupported("IsClient");
    }

    bool GetConsoleUsed() override
    {
        Unsupported("GetConsoleUsed");
    }

    int GetSnapRate() override
    {
        Unsupported("GetSnapRate");
    }

    void NetReceiveReliable( int peer, int type, idBitMsg & msg ) override
    {
        Unsupported("NetReceiveReliable");
    }

    void NetReceiveSnapshot( class idSnapShot & ss ) override
    {
        Unsupported("NetReceiveSnapshot");
    }

    void NetReceiveUsercmds( int peer, idBitMsg & msg ) override
    {
        Unsupported("NetReceiveUsercmds");
    }

    bool ProcessEvent( const sysEvent_t * event ) override
    {
        Unsupported("ProcessEvent");
    }

    bool LoadGame( const char * saveName ) override
    {
        Unsupported("LoadGame");
    }

    bool SaveGame( const char * saveName ) override
    {
        Unsupported("SaveGame");
    }

    idDemoFile * ReadDemo() override
    {
        Unsupported("ReadDemo");
    }

    idDemoFile * WriteDemo() override
    {
        Unsupported("WriteDemo");
    }

    idGame * Game() override
    {
        Unsupported("Game");
    }

    idRenderWorld * RW() override
    {
        Unsupported("RW");
    }

    idSoundWorld * SW() override
    {
        Unsupported("SW");
    }

    idSoundWorld * MenuSW() override
    {
        Unsupported("MenuSW");
    }

    idSession * Session() override
    {
        Unsupported("Session");
    }

    idCommonDialog & Dialog() override
    {
        Unsupported("Dialog");
    }

    void OnSaveCompleted( idSaveLoadParms & parms ) override
    {
        Unsupported("OnSaveCompleted");
    }

    void OnLoadCompleted( idSaveLoadParms & parms ) override
    {
        Unsupported("OnLoadCompleted");
    }

    void OnLoadFilesCompleted( idSaveLoadParms & parms ) override
    {
        Unsupported("OnLoadFilesCompleted");
    }

    void OnEnumerationCompleted( idSaveLoadParms & parms ) override
    {
        Unsupported("OnEnumerationCompleted");
    }

    void OnDeleteCompleted( idSaveLoadParms & parms ) override
    {
        Unsupported("OnDeleteCompleted");
    }

    void TriggerScreenWipe( const char * _wipeMaterial, bool hold ) override
    {
        Unsupported("TriggerScreenWipe");
    }

    void OnStartHosting( idMatchParameters & parms ) override
    {
        Unsupported("OnStartHosting");
    }

    int GetGameFrame() override
    {
        Unsupported("GetGameFrame");
    }

    void LaunchExternalTitle( int titleIndex, int device, const lobbyConnectInfo_t * const connectInfo ) override
    {
        Unsupported("LaunchExternalTitle");
    }

    void InitializeMPMapsModes() override
    {
        Unsupported("InitializeMPMapsModes");
    }

    const idStrList & GetModeList() const override
    {
        Unsupported("GetModeList");
    }

    const idStrList & GetModeDisplayList() const override
    {
        Unsupported("GetModeDisplayList");
    }

    const idList<mpMap_t> & GetMapList() const override
    {
        Unsupported("GetMapList");
    }

    void ResetPlayerInput( int playerIndex ) override
    {
        Unsupported("ResetPlayerInput");
    }

    bool JapaneseCensorship() const override
    {
        Unsupported("JapaneseCensorship");
    }

    void QueueShowShell() override
    {
        Unsupported("QueueShowShell");
    }

    currentGame_t GetCurrentGame() const override
    {
        Unsupported("GetCurrentGame");
    }

    void SwitchToGame( currentGame_t newGame ) override
    {
        Unsupported("SwitchToGame");
    }
};

class CookFileSystem final : public idFileSystem
{
public:
    explicit CookFileSystem(cook::SourceReader reader) : m_reader(reader)
    {
    }

    int ReadFile(const char * relativePath, void ** buffer, ID_TIME_T * timestamp) override
    {
        std::vector<std::uint8_t> data;
        if (!m_reader(relativePath, data))
        {
            if (buffer != nullptr)
            {
                *buffer = nullptr;
            }

            return -1;
        }

        if (timestamp != nullptr)
        {
            *timestamp = 0;
        }

        if (buffer != nullptr)
        {
            char * const text = static_cast<char *>(Mem_Alloc(static_cast<int>(data.size()) + 1, TAG_IDFILE));
            std::memcpy(text, data.data(), data.size());
            text[data.size()] = '\0';
            *buffer = text;
        }

        return static_cast<int>(data.size());
    }

    void FreeFile(void * buffer) override
    {
        Mem_Free(buffer);
    }

    idFile * OpenFileRead(const char * relativePath, bool, const char *) override
    {
        std::vector<std::uint8_t> data;
        if (!m_reader(relativePath, data))
        {
            return nullptr;
        }

        return new CookFile(relativePath, std::move(data));
    }

    idFile * OpenExplicitFileRead(const char * path) override
    {
        return OpenFileRead(path, false, nullptr);
    }

    void CloseFile(idFile * file) override
    {
        delete file;
    }

    // The cook's paths are the relative names themselves, as the PS2's are once mounted.
    const char * OSPathToRelativePath(const char * path) override
    {
        return path;
    }

    const char * RelativePathToOSPath(const char * relativePath, const char *) override
    {
        return relativePath;
    }

    void Init() override
    {
        Unsupported("Init");
    }

    void Restart() override
    {
        Unsupported("Restart");
    }

    void Shutdown( bool reloading ) override
    {
        Unsupported("Shutdown");
    }

    bool IsInitialized() const override
    {
        Unsupported("IsInitialized");
    }

    idFileList * ListFiles( const char *relativePath, const char *extension, bool sort, bool fullRelativePath, const char* gamedir) override
    {
        Unsupported("ListFiles");
    }

    idFileList * ListFilesTree( const char *relativePath, const char *extension, bool sort, const char* gamedir) override
    {
        Unsupported("ListFilesTree");
    }

    void FreeFileList( idFileList *fileList ) override
    {
        Unsupported("FreeFileList");
    }

    const char * BuildOSPath( const char *base, const char *game, const char *relativePath ) override
    {
        Unsupported("BuildOSPath");
    }

    const char * BuildOSPath( const char *base, const char *relativePath ) override
    {
        Unsupported("BuildOSPath");
    }

    void CreateOSPath( const char *OSPath ) override
    {
        Unsupported("CreateOSPath");
    }

    int WriteFile( const char *relativePath, const void *buffer, int size, const char *basePath) override
    {
        Unsupported("WriteFile");
    }

    void RemoveFile( const char *relativePath ) override
    {
        Unsupported("RemoveFile");
    }

    bool RemoveDir( const char * relativePath ) override
    {
        Unsupported("RemoveDir");
    }

    bool RenameFile( const char * relativePath, const char * newName, const char * basePath) override
    {
        Unsupported("RenameFile");
    }

    idFile * OpenFileReadMemory( const char *relativePath, bool allowCopyFiles, const char* gamedir) override
    {
        Unsupported("OpenFileReadMemory");
    }

    idFile * OpenFileWrite( const char *relativePath, const char *basePath) override
    {
        Unsupported("OpenFileWrite");
    }

    idFile * OpenFileAppend( const char *filename, bool sync, const char *basePath) override
    {
        Unsupported("OpenFileAppend");
    }

    idFile * OpenFileByMode( const char *relativePath, fsMode_t mode ) override
    {
        Unsupported("OpenFileByMode");
    }

    idFile * OpenExplicitFileWrite( const char *OSPath ) override
    {
        Unsupported("OpenExplicitFileWrite");
    }

    idFile_Cached * OpenExplicitPakFile( const char *OSPath ) override
    {
        Unsupported("OpenExplicitPakFile");
    }

    void FindDLL( const char *basename, char dllPath[ MAX_OSPATH ] ) override
    {
        Unsupported("FindDLL");
    }

    void CopyFile( const char *fromOSPath, const char *toOSPath ) override
    {
        Unsupported("CopyFile");
    }

    findFile_t FindFile( const char *path ) override
    {
        Unsupported("FindFile");
    }

    bool FilenameCompare( const char *s1, const char *s2 ) const override
    {
        Unsupported("FilenameCompare");
    }

    int GetFileLength( const char * relativePath ) override
    {
        Unsupported("GetFileLength");
    }

    sysFolder_t IsFolder( const char * relativePath, const char *basePath) override
    {
        Unsupported("IsFolder");
    }

    void EnableBackgroundCache( bool enable ) override
    {
        Unsupported("EnableBackgroundCache");
    }

    void BeginLevelLoad( const char *name, char *_blockBuffer, int _blockBufferSize ) override
    {
        Unsupported("BeginLevelLoad");
    }

    void EndLevelLoad() override
    {
        Unsupported("EndLevelLoad");
    }

    bool InProductionMode() override
    {
        Unsupported("InProductionMode");
    }

    bool UsingResourceFiles() override
    {
        Unsupported("UsingResourceFiles");
    }

    void UnloadMapResources( const char *name ) override
    {
        Unsupported("UnloadMapResources");
    }

    void UnloadResourceContainer( const char *name ) override
    {
        Unsupported("UnloadResourceContainer");
    }

    void StartPreload( const idStrList &_preload ) override
    {
        Unsupported("StartPreload");
    }

    void StopPreload() override
    {
        Unsupported("StopPreload");
    }

    int ReadFromBGL( idFile *_resourceFile, void * _buffer, int _offset, int _len ) override
    {
        Unsupported("ReadFromBGL");
    }

    bool IsBinaryModel( const idStr & resName ) const override
    {
        Unsupported("IsBinaryModel");
    }

    bool IsSoundSample( const idStr & resName ) const override
    {
        Unsupported("IsSoundSample");
    }

    bool GetResourceCacheEntry( const char *fileName, idResourceCacheEntry &rc ) override
    {
        Unsupported("GetResourceCacheEntry");
    }

    void FreeResourceBuffer() override
    {
        Unsupported("FreeResourceBuffer");
    }

    void AddImagePreload( const char *resName, int filter, int repeat, int usage, int cube ) override
    {
        Unsupported("AddImagePreload");
    }

    void AddSamplePreload( const char *resName ) override
    {
        Unsupported("AddSamplePreload");
    }

    void AddModelPreload( const char *resName ) override
    {
        Unsupported("AddModelPreload");
    }

    void AddAnimPreload( const char *resName ) override
    {
        Unsupported("AddAnimPreload");
    }

    void AddParticlePreload( const char *resName ) override
    {
        Unsupported("AddParticlePreload");
    }

    void AddCollisionPreload( const char *resName ) override
    {
        Unsupported("AddCollisionPreload");
    }

private:
    cook::SourceReader m_reader;
};

// Only the compile timer reaches the system interface: the clock, in microseconds.
class CookSys final : public idSys
{
public:
    void DebugPrintf(const char * format, ...) override
    {
        va_list arguments;
        va_start(arguments, format);
        PrintV(stderr, "", format, arguments);
        va_end(arguments);
    }

    void DebugVPrintf(const char * format, va_list arguments) override
    {
        PrintV(stderr, "", format, arguments);
    }

    double GetClockTicks() override
    {
        using namespace std::chrono;
        return static_cast<double>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
    }

    double ClockTicksPerSecond() override
    {
        return 1000000.0;
    }

    cpuid_t GetProcessorId() override
    {
        Unsupported("GetProcessorId");
    }

    const char * GetProcessorString() override
    {
        Unsupported("GetProcessorString");
    }

    const char * FPU_GetState() override
    {
        Unsupported("FPU_GetState");
    }

    bool FPU_StackIsEmpty() override
    {
        Unsupported("FPU_StackIsEmpty");
    }

    void FPU_SetFTZ( bool enable ) override
    {
        Unsupported("FPU_SetFTZ");
    }

    void FPU_SetDAZ( bool enable ) override
    {
        Unsupported("FPU_SetDAZ");
    }

    void FPU_EnableExceptions( int exceptions ) override
    {
        Unsupported("FPU_EnableExceptions");
    }

    bool LockMemory( void *ptr, int bytes ) override
    {
        Unsupported("LockMemory");
    }

    bool UnlockMemory( void *ptr, int bytes ) override
    {
        Unsupported("UnlockMemory");
    }

    void GetCallStack( address_t *callStack, const int callStackSize ) override
    {
        Unsupported("GetCallStack");
    }

    const char * GetCallStackStr( const address_t *callStack, const int callStackSize ) override
    {
        Unsupported("GetCallStackStr");
    }

    const char * GetCallStackCurStr( int depth ) override
    {
        Unsupported("GetCallStackCurStr");
    }

    void ShutdownSymbols() override
    {
        Unsupported("ShutdownSymbols");
    }

    int DLL_Load( const char *dllName ) override
    {
        Unsupported("DLL_Load");
    }

    void * DLL_GetProcAddress( int dllHandle, const char *procName ) override
    {
        Unsupported("DLL_GetProcAddress");
    }

    void DLL_Unload( int dllHandle ) override
    {
        Unsupported("DLL_Unload");
    }

    void DLL_GetFileName( const char *baseName, char *dllName, int maxLength ) override
    {
        Unsupported("DLL_GetFileName");
    }

    sysEvent_t GenerateMouseButtonEvent( int button, bool down ) override
    {
        Unsupported("GenerateMouseButtonEvent");
    }

    sysEvent_t GenerateMouseMoveEvent( int deltax, int deltay ) override
    {
        Unsupported("GenerateMouseMoveEvent");
    }

    void OpenURL( const char *url, bool quit ) override
    {
        Unsupported("OpenURL");
    }

    void StartProcess( const char *exePath, bool quit ) override
    {
        Unsupported("StartProcess");
    }
};

static CookCommon s_common;
static CookSys s_sys;

} // namespace

// The engine globals Common.cpp, FileSystem.cpp and CVarSystem.cpp define in the game. The few
// idlib cvars only join the static list; nothing registers them.
idCommon * common = nullptr;
idFileSystem * fileSystem = nullptr;
idCVarSystem * cvarSystem = nullptr;
idCVar * idCVar::staticVars = nullptr;

// ------------------------------------------------------------------------------------------------
// idFile's own methods: the cook links no File.cpp (it needs zlib), and CookFile overrides the
// few the lexer calls.
// ------------------------------------------------------------------------------------------------

const char * idFile::GetName() const
{
    Unsupported("idFile::GetName");
}

const char * idFile::GetFullPath() const
{
    Unsupported("idFile::GetFullPath");
}

int idFile::Read( void *buffer, int len )
{
    Unsupported("idFile::Read");
}

int idFile::Write( const void *buffer, int len )
{
    Unsupported("idFile::Write");
}

int idFile::Length() const
{
    Unsupported("idFile::Length");
}

ID_TIME_T idFile::Timestamp() const
{
    Unsupported("idFile::Timestamp");
}

int idFile::Tell() const
{
    Unsupported("idFile::Tell");
}

void idFile::ForceFlush()
{
    Unsupported("idFile::ForceFlush");
}

void idFile::Flush()
{
    Unsupported("idFile::Flush");
}

int idFile::Seek( long offset, fsOrigin_t origin )
{
    Unsupported("idFile::Seek");
}

void idFile::Rewind()
{
    Unsupported("idFile::Rewind");
}

int idFile::Printf( const char *fmt, ... )
{
    Unsupported("idFile::Printf");
}

int idFile::VPrintf( const char *fmt, va_list arg )
{
    Unsupported("idFile::VPrintf");
}

int idFile::WriteFloatString( const char *fmt, ... )
{
    Unsupported("idFile::WriteFloatString");
}

int idFile::ReadInt( int &value )
{
    Unsupported("idFile::ReadInt");
}

int idFile::ReadUnsignedInt( unsigned int &value )
{
    Unsupported("idFile::ReadUnsignedInt");
}

int idFile::ReadShort( short &value )
{
    Unsupported("idFile::ReadShort");
}

int idFile::ReadUnsignedShort( unsigned short &value )
{
    Unsupported("idFile::ReadUnsignedShort");
}

int idFile::ReadChar( char &value )
{
    Unsupported("idFile::ReadChar");
}

int idFile::ReadUnsignedChar( unsigned char &value )
{
    Unsupported("idFile::ReadUnsignedChar");
}

int idFile::ReadFloat( float &value )
{
    Unsupported("idFile::ReadFloat");
}

int idFile::ReadBool( bool &value )
{
    Unsupported("idFile::ReadBool");
}

int idFile::ReadString( idStr &string )
{
    Unsupported("idFile::ReadString");
}

int idFile::ReadVec2( idVec2 &vec )
{
    Unsupported("idFile::ReadVec2");
}

int idFile::ReadVec3( idVec3 &vec )
{
    Unsupported("idFile::ReadVec3");
}

int idFile::ReadVec4( idVec4 &vec )
{
    Unsupported("idFile::ReadVec4");
}

int idFile::ReadVec6( idVec6 &vec )
{
    Unsupported("idFile::ReadVec6");
}

int idFile::ReadMat3( idMat3 &mat )
{
    Unsupported("idFile::ReadMat3");
}

int idFile::WriteInt( const int value )
{
    Unsupported("idFile::WriteInt");
}

int idFile::WriteUnsignedInt( const unsigned int value )
{
    Unsupported("idFile::WriteUnsignedInt");
}

int idFile::WriteShort( const short value )
{
    Unsupported("idFile::WriteShort");
}

int idFile::WriteUnsignedShort( unsigned short value )
{
    Unsupported("idFile::WriteUnsignedShort");
}

int idFile::WriteChar( const char value )
{
    Unsupported("idFile::WriteChar");
}

int idFile::WriteUnsignedChar( const unsigned char value )
{
    Unsupported("idFile::WriteUnsignedChar");
}

int idFile::WriteFloat( const float value )
{
    Unsupported("idFile::WriteFloat");
}

int idFile::WriteBool( const bool value )
{
    Unsupported("idFile::WriteBool");
}

int idFile::WriteString( const char *string )
{
    Unsupported("idFile::WriteString");
}

int idFile::WriteVec2( const idVec2 &vec )
{
    Unsupported("idFile::WriteVec2");
}

int idFile::WriteVec3( const idVec3 &vec )
{
    Unsupported("idFile::WriteVec3");
}

int idFile::WriteVec4( const idVec4 &vec )
{
    Unsupported("idFile::WriteVec4");
}

int idFile::WriteVec6( const idVec6 &vec )
{
    Unsupported("idFile::WriteVec6");
}

int idFile::WriteMat3( const idMat3 &mat )
{
    Unsupported("idFile::WriteMat3");
}

// ------------------------------------------------------------------------------------------------
// idLib's statics and console wrappers, which Lib.cpp defines beside the engine's init.
// ------------------------------------------------------------------------------------------------

idSys * idLib::sys = nullptr;
idCommon * idLib::common = nullptr;
idCVarSystem * idLib::cvarSystem = nullptr;
idFileSystem * idLib::fileSystem = nullptr;
int idLib::frameNumber = 0;
bool idLib::mainThreadInitialized = false;
ID_TLS idLib::isMainThread = 0;

void idLib::Printf(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintV(stdout, "", format, arguments);
    va_end(arguments);
}

void idLib::PrintfIf(const bool test, const char * format, ...)
{
    if (!test)
    {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    PrintV(stdout, "", format, arguments);
    va_end(arguments);
}

void idLib::Error(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FailV(format, arguments);
}

void idLib::FatalError(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FailV(format, arguments);
}

void idLib::Warning(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintV(stderr, "script_cook: warning: ", format, arguments);
    va_end(arguments);
    std::fprintf(stderr, "\n");
}

void idLib::WarningIf(const bool test, const char * format, ...)
{
    if (!test)
    {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    PrintV(stderr, "script_cook: warning: ", format, arguments);
    va_end(arguments);
    std::fprintf(stderr, "\n");
}

bool AssertFailed(const char * file, int line, const char * expression)
{
    std::fprintf(stderr, "script_cook: assertion failed at %s:%d: %s\n", file, line, expression);
    std::exit(3);
}

int Sys_Milliseconds()
{
    using namespace std::chrono;
    static const steady_clock::time_point s_start = steady_clock::now();
    return static_cast<int>(duration_cast<milliseconds>(steady_clock::now() - s_start).count());
}

// ------------------------------------------------------------------------------------------------
// Entry points
// ------------------------------------------------------------------------------------------------

namespace cook
{

void InitEngine(SourceReader reader, bool verbose)
{
    static CookFileSystem s_fileSystem(reader);
    s_verbose = verbose;
    common = &s_common;
    fileSystem = &s_fileSystem;
    idLib::common = &s_common;
    idLib::fileSystem = &s_fileSystem;
    idLib::sys = &s_sys;
}

} // namespace cook
