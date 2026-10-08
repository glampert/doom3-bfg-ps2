// ================================================================================================
// File: core_boot.cpp
// Brief: Exercise genuine commands, CVars and fixture reads alongside shared heap/idlib regressions.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "tests/smoketests/core_boot.h"
#include "tests/smoketests/heap_tests.h"
#include "tests/smoketests/idlib_tests.h"
#include "ps2/system/core.h"
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
    printf("[D3BFG] CHECK core/%s %s\n", name, passed ? "PASS" : "FAIL");
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
        // idFile_Memory takes a positive backward distance for FS_SEEK_END.
        seekRead = file->Seek(8, FS_SEEK_END) == 0 && file->Read(ending, 8) == 8 &&
            idStr::Cmp(ending, "fixture\n") == 0 && file->Tell() == length && file->Read(ending, 1) == 0;
        fileSystem->CloseFile(file);
    }
    return Check("filesystem-lexer-fixture", contents && lexerTokens && seekRead);
}

bool CheckMissingAndEscapingPaths()
{
    void * data = reinterpret_cast<void *>(1);
    const int missingLength = fileSystem->ReadFile("ps2-smoke-does-not-exist.txt", &data, nullptr);
    const bool missing = missingLength == -1 && data == nullptr;
    const bool parentRejected = fileSystem->ReadFile("../fixture.txt", nullptr, nullptr) == -1;
    const bool absoluteRejected = fileSystem->ReadFile("host:/fixture.txt", nullptr, nullptr) == -1;
    const bool otherDeviceRejected = fileSystem->ReadFile("mass:fixture.txt", nullptr, nullptr) == -1;
    return Check("filesystem-missing-path-jail", missing && parentRejected && absoluteRejected && otherDeviceRejected);
}

} // namespace

bool RunCoreTests(const char * fixturePath)
{
    core::PrintMemory("before-core");
    bool passed = RunHeapTests();
    const heap::Stats baseline = heap::GetTotalStats();
    core::Init();
    core::PrintMemory("initialized-core");
    passed = Check("initialized", common->IsInitialized() && fileSystem->IsInitialized()) && passed;
    passed = CheckCommands() && passed;
    passed = CheckCVars() && passed;
    passed = CheckParser() && passed;
    passed = CheckFixture(fixturePath) && passed;
    passed = CheckMissingAndEscapingPaths() && passed;
    passed = RunIdlibTests() && passed;
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
