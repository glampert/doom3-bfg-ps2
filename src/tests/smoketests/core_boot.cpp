// ================================================================================================
// File: core_boot.cpp
// Brief: Exercise genuine commands, CVars and fixture reads alongside shared heap/idlib regressions.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"

#include "tests/smoketests/core_boot.h"
#include "tests/smoketests/audio_tests.h"
#include "tests/smoketests/renderer_tests.h"
#include "tests/smoketests/deferred_tests.h"
#include "tests/smoketests/offline_tests.h"
#include "tests/smoketests/type_query_tests.h"
#include "tests/smoketests/class_alloc_tests.h"
#include "tests/smoketests/heap_tests.h"
#include "tests/smoketests/idlib_tests.h"
#include "ps2/system/core.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"

#include <idlib/precompiled.h>

namespace ps2::smoketests
{
namespace
{

static int s_commandCount = 0;
static int s_commandValue = 0;

void TestCommand(const idCmdArgs & arguments)
{
    ++s_commandCount;
    s_commandValue = arguments.Argc() == 2 ? atoi(arguments.Argv(1)) : -1;
}

bool Check(const char * name, bool passed)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK core/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool CheckCommands()
{
    s_commandCount = 0;
    s_commandValue = 0;
    cmdSystem->AddCommand("ps2_smoke_command", TestCommand, CMD_FL_SYSTEM, "headless command fixture");
    cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "ps2_smoke_command 41; wait 1; ps2_smoke_command 42\n");
    cmdSystem->ExecuteCommandBuffer();
    const bool first = s_commandCount == 1 && s_commandValue == 41;
    cmdSystem->ExecuteCommandBuffer();
    const bool second = s_commandCount == 2 && s_commandValue == 42;
    const bool buffered = Check("command-buffer-wait", first && second);
    s_commandCount = 0;
    s_commandValue = 0;
    cmdSystem->BufferCommandText(CMD_EXEC_APPEND, "exec smoke.cfg\n");
    cmdSystem->ExecuteCommandBuffer();
    const bool executed = Check("exec-config", s_commandCount == 1 && s_commandValue == 77 &&
        cvarSystem->GetCVarInteger("ps2_smoke_config") == 37);
    cmdSystem->RemoveCommand("ps2_smoke_command");
    return buffered && executed;
}

bool CheckCVars()
{
    cvarSystem->SetCVarInteger("ps2_smoke_dynamic", 7);
    cmdSystem->BufferCommandText(CMD_EXEC_NOW, "set ps2_smoke_dynamic 19\n");
    const bool changed = cvarSystem->GetCVarInteger("ps2_smoke_dynamic") == 19;
    idCVar * registered = cvarSystem->Find("net_allowCheats");
    const bool staticRegistered = registered != nullptr && registered->GetBool() == false;
    idCVar bounded("ps2_smoke_bounded", "2", CVAR_INTEGER | CVAR_NOCHEAT,
        "synthetic range fixture", 0.0f, 5.0f);
    idCVar readOnly("ps2_smoke_readonly", "11", CVAR_INTEGER | CVAR_ROM,
        "synthetic read-only fixture");
    cmdSystem->BufferCommandText(CMD_EXEC_NOW, "set ps2_smoke_bounded 99\n");
    // Direct cvar commands respect ROM; the engine's set/programmatic APIs deliberately force writes.
    cmdSystem->BufferCommandText(CMD_EXEC_NOW, "ps2_smoke_readonly 99\n");
    const bool constraints = bounded.GetInteger() == 5 && readOnly.GetInteger() == 11;
    cvarSystem->SetCVarInteger("ps2_smoke_readonly", 17);
    const bool forced = readOnly.GetInteger() == 17;
    return Check("cvar-registration-command", changed && staticRegistered && constraints && forced && cvarSystem->IsInitialized());
}

bool CheckPlatformServices()
{
    const bool languages = Sys_NumLangs() == 6 && idStr::Cmp(Sys_Lang(0), ID_LANG_ENGLISH) == 0 &&
        idStr::Cmp(Sys_Lang(5), ID_LANG_JAPANESE) == 0 && Sys_Lang(-1)[0] == '\0' && Sys_Lang(6)[0] == '\0' &&
        idStr::Cmp(Sys_DefaultLanguage(), ID_LANG_ENGLISH) == 0 &&
        idStr::Cmp(cvarSystem->GetCVarString("sys_lang"), ID_LANG_ENGLISH) == 0 &&
        (cvarSystem->Find("sys_lang")->GetFlags() & CVAR_INIT) != 0;
    const bool durations = idStr::Cmp(Sys_SecToStr(0), "0:00:00") == 0 &&
        idStr::Cmp(Sys_SecToStr(3661), "1:01:01") == 0 &&
        idStr::Cmp(Sys_SecToStr(90061), "1d, 1:01:01") == 0 &&
        idStr::Cmp(Sys_SecToStr(2147483647), "3550w, 5d, 3:14:07") == 0;
    const bool timestamps = idStr::Cmp(Sys_TimeStampToStr(-1), "timestamp unavailable") == 0 &&
        idStr::Cmp(Sys_TimeStampToStr(0), "1970-01-01 00:00 UTC") == 0 &&
        idStr::Cmp(Sys_TimeStampToStr(951782400), "2000-02-29 00:00 UTC") == 0 &&
        idStr::Cmp(Sys_TimeStampToStr(2147483647), "2038-01-19 03:14 UTC") == 0;
    return Check("platform-language-duration-utc", languages && durations && timestamps);
}

bool CheckParser()
{
    constexpr char kSource[] = "#define SMOKE 42\nSMOKE\n";
    idParser parser(LEXFL_NOERRORS | LEXFL_NOWARNINGS);
    const bool loaded = parser.LoadMemory(kSource, static_cast<int>(sizeof(kSource) - 1), "core-preprocessor");
    idToken token;
    const bool macro = loaded && parser.ReadToken(&token) && token == "42" && !parser.ReadToken(&token);
    constexpr char kInvalid[] = "\"unterminated";
    idLexer invalid(kInvalid, static_cast<int>(sizeof(kInvalid) - 1), "core-invalid-lexer",
        LEXFL_NOERRORS | LEXFL_NOWARNINGS);
    const bool rejected = !invalid.ReadToken(&token) && invalid.HadError();
    return Check("parser-macro-error", macro && rejected);
}

bool CheckFixture(const char * path)
{
    void * data = nullptr;
    const int length = fileSystem->ReadFile(path, &data, nullptr);
    constexpr char kExpected[] = "D3BFG core fixture\n";
    const bool loaded = length == static_cast<int>(sizeof(kExpected) - 1) && data != nullptr;
    const bool contents = loaded && memcmp(data, kExpected, sizeof(kExpected)) == 0;
    fileSystem->FreeFile(data);

    idLexer lexer(LEXFL_NOERRORS | LEXFL_NOWARNINGS);
    const bool lexerLoaded = lexer.LoadFile(path);
    idToken token;
    const bool lexerTokens = lexerLoaded && lexer.ReadToken(&token) && token == "D3BFG" &&
        lexer.ReadToken(&token) && token == "core" && lexer.ReadToken(&token) && token == "fixture" &&
        !lexer.ReadToken(&token);
    idFile * file = fileSystem->OpenFileRead(path, false, nullptr);
    bool seekRead = false;
    if (file != nullptr)
    {
        char ending[9] = {};
        // Permanent streams use stdio's signed end offset.
        seekRead = file->Seek(-8, FS_SEEK_END) == 0 && file->Read(ending, 8) == 8 &&
            idStr::Cmp(ending, "fixture\n") == 0 && file->Tell() == length && file->Read(ending, 1) == 0;
        fileSystem->CloseFile(file);
    }
    idFile * memory = fileSystem->OpenFileReadMemory(path, false, nullptr);
    bool memorySeek = false;
    if (memory != nullptr)
    {
        char ending[9] = {};
        // Memory files retain the original positive backward-distance API.
        memorySeek = memory->Seek(8, FS_SEEK_END) == 0 && memory->Read(ending, 8) == 8 &&
            idStr::Cmp(ending, "fixture\n") == 0;
        fileSystem->CloseFile(memory);
    }
    return Check("filesystem-lexer-fixture", contents && lexerTokens && seekRead && memorySeek);
}

bool CheckFilesystemServices()
{
    namespace fs = ps2::filesystem;
    const heap::Stats baseline = heap::GetTotalStats();
    idFile * stream = fileSystem->OpenFileRead("fs-fixtures/large.bin", false, nullptr);
    bool streamed = false;
    if (stream != nullptr)
    {
        const heap::Stats opened = heap::GetTotalStats();
        char bytes[17] = {};
        streamed = stream->Length() == 71680 && stream->Seek(-16, FS_SEEK_END) == 0 &&
            stream->Read(bytes, 16) == 16 && stream->Read(bytes, 1) == 0 &&
            memcmp(bytes, "ZZZZZZZZZZZZZZZZ", 16) == 0 &&
            opened.requestedBytes < baseline.requestedBytes + 4096;
        fileSystem->CloseFile(stream);
    }
    void * data = reinterpret_cast<void *>(1);
    const bool boundedMemory = fileSystem->ReadFile("fs-fixtures/large.bin", &data, nullptr) == -1 && data == nullptr &&
        fileSystem->GetFileLength("fs-fixtures/large.bin") == 71680;
    const heap::Stats closed = heap::GetTotalStats();
    bool passed = Check("filesystem-streaming-length", streamed && boundedMemory &&
        closed.requestedBytes == baseline.requestedBytes && closed.allocationCount == baseline.allocationCount);

    idStrList names;
    const bool files = Sys_ListFiles("host:fs-fixtures", ".txt", names) == 1 && names[0] == "mixed.TxT";
    const bool folders = Sys_ListFiles("host:fs-fixtures", "/", names) == 1 && names[0] == "child";
    const bool missing = Sys_ListFiles("host:fs-fixtures/missing", "", names) == -1 && names.Num() == 0;
    const bool kinds = Sys_IsFolder("host:fs-fixtures") == FOLDER_YES &&
        Sys_IsFolder("host:fs-fixtures/mixed.TxT") == FOLDER_NO &&
        Sys_IsFolder("host:fs-fixtures/missing") == FOLDER_ERROR;
    passed = Check("filesystem-directory-filters", files && folders && missing && kinds) && passed;

    char path[MAX_OSPATH];
    const bool paths = fs::BuildPath(path, sizeof(path), "host:", "base", "maps/test.map") &&
        strcmp(path, "host:/base/maps/test.map") == 0 &&
        !fs::BuildPath(path, sizeof(path), "host:", "base", "../maps/test.map") && path[0] == '\0';
    passed = Check("filesystem-device-path-bounds", paths) && passed;

    FILE * file = fs::Open("host:fs-fixtures/written.bin", fs::OpenMode::Write);
    bool written = file != nullptr;
    if (file != nullptr)
    {
        written = fwrite("abc", 1, 3, file) == 3;
        written = fclose(file) == 0 && written;
    }
    file = fs::Open("host:fs-fixtures/written.bin", fs::OpenMode::Append);
    bool appended = file != nullptr;
    if (file != nullptr)
    {
        appended = ftell(file) == 3 && fwrite("def", 1, 3, file) == 3;
        appended = fclose(file) == 0 && appended;
    }
    file = fs::Open("host:fs-fixtures/written.bin", fs::OpenMode::Read);
    bool read = file != nullptr;
    if (file != nullptr)
    {
        char bytes[7] = {};
        read = fseek(file, 2, SEEK_SET) == 0 && fs::FileLength(file) == 6 && ftell(file) == 2 &&
            fs::ReadExact(file, bytes, 4) && strcmp(bytes, "cdef") == 0 &&
            fseek(file, 0, SEEK_SET) == 0 && !fs::ReadExact(file, bytes, 7);
        read = fclose(file) == 0 && read;
    }
    passed = Check("filesystem-write-append-short-read", written && appended && read) && passed;

    const bool parents = fs::CreateParents("host:fs-fixtures/new-dir/file.bin") &&
        Sys_IsFolder("host:fs-fixtures/new-dir") == FOLDER_YES && Sys_Rmdir("host:fs-fixtures/new-dir");
    errno = 0;
    const bool noRename = !fs::Rename("host:fs-fixtures/written.bin", "host:fs-fixtures/renamed.bin") && errno == ENOSYS;
    errno = 0;
    const bool removed = fs::Remove("host:fs-fixtures/written.bin");
    const int removeError = errno;
    const sysFolder_t removedKind = Sys_IsFolder("host:fs-fixtures/written.bin");
    idFile * removedFile = fileSystem->OpenFileRead("fs-fixtures/written.bin", false, nullptr);
    const bool absent = removedFile == nullptr;
    fileSystem->CloseFile(removedFile);
    // PCSX2 2.6.3 HostFs deletes the file but reports ENODEV. Require the exact error and
    // absence of both file and directory, while keeping the production failure observable.
    const bool removalResult = removed || (!removed && removeError == ENODEV);
    if (!removed || !(parents && noRename && removedKind == FOLDER_ERROR && absent))
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] FILESYSTEM parents=%d rename_unsupported=%d remove=%d errno=%d kind=%d absent=%d\n",
            parents ? 1 : 0, noRename ? 1 : 0, removed ? 1 : 0, removeError, static_cast<int>(removedKind), absent ? 1 : 0);
    }
    passed = Check("filesystem-parents-driver-errors", fs::Init() && parents && noRename && removalResult &&
        removedKind == FOLDER_ERROR && absent) && passed;

    std::uint32_t time = 0;
    const bool timestamp = fs::ZipTime("host:fs-fixtures/mixed.TxT", time) && time == (1u << 21 | 1u << 16) &&
        !fs::ZipTime("host:fs-fixtures/missing", time) && time == 0;
    return Check("filesystem-zip-timestamp", timestamp) && passed;
}

bool CheckMissingAndEscapingPaths()
{
    void * data = reinterpret_cast<void *>(1);
    const int missingLength = fileSystem->ReadFile("ps2-smoke-does-not-exist.txt", &data, nullptr);
    const bool missing = missingLength == -1 && data == nullptr;
    const bool parentRejected = fileSystem->ReadFile("../fixture.txt", nullptr, nullptr) == -1;
    const bool absoluteRejected = fileSystem->ReadFile("host:/fixture.txt", nullptr, nullptr) == -1;
    const bool otherDeviceRejected = fileSystem->ReadFile("mass:fixture.txt", nullptr, nullptr) == -1;
    const bool pathMapping = idStr::Cmp(fileSystem->OSPathToRelativePath("host:script/fixture.script"), "script/fixture.script") == 0 &&
        idStr::Cmp(fileSystem->RelativePathToOSPath("script/fixture.script"), "host:script/fixture.script") == 0 &&
        fileSystem->OSPathToRelativePath("../fixture.script")[0] == '\0' &&
        fileSystem->OSPathToRelativePath("mass:fixture.script")[0] == '\0';
    return Check("filesystem-missing-path-jail", missing && parentRejected && absoluteRejected && otherDeviceRejected && pathMapping);
}

} // namespace

bool RunCoreTests(const char * fixturePath)
{
    if (IsCommonProbe(fixturePath)) { return RunCommonProbe(fixturePath); }
    core::PrintMemory("before-core");
    bool passed = RunHeapTests();
    passed = RunClassAllocTests() && passed;
    const heap::Stats baseline = heap::GetTotalStats();
    core::Init();
    core::PrintMemory("initialized-core");
    passed = Check("initialized", common->IsInitialized() && fileSystem->IsInitialized()) && passed;
    passed = RunOfflineTests() && passed;
    passed = RunAudioTests() && passed;
    passed = RunRendererTests() && passed;
    passed = RunDeferredTests() && passed;
    passed = CheckCommands() && passed;
    passed = CheckCVars() && passed;
    passed = CheckPlatformServices() && passed;
    passed = CheckParser() && passed;
    passed = CheckFixture(fixturePath) && passed;
    passed = CheckMissingAndEscapingPaths() && passed;
    passed = CheckFilesystemServices() && passed;
    passed = RunIdlibTests() && passed;
    passed = RunTypeQueryTests() && passed;
    passed = RunEngineTypeQueryTests() && passed;
    core::PrintMemory("tested-core");
    core::Shutdown();
    const heap::Stats afterShutdown = heap::GetTotalStats();
    passed = Check("shutdown", !common->IsInitialized() && !fileSystem->IsInitialized() &&
        baseline.requestedBytes == afterShutdown.requestedBytes &&
        baseline.backingBytes == afterShutdown.backingBytes &&
        baseline.allocationCount == afterShutdown.allocationCount) && passed;
    core::PrintMemory("after-core-shutdown");
    return passed;
}

} // namespace ps2::smoketests
