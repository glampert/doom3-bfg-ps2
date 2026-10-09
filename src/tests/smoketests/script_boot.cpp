// ================================================================================================
// File: script_boot.cpp
// Brief: Run the real script compiler/program in isolation, with an explicitly empty event registry.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "idlib/precompiled.h"
#include "d3xp/Game_local.h"
#include "ps2/game/script_error.h"
#include "ps2/system/core.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <sifrpc.h>
#include <kernel.h>
#include <cstdio>
#include <cstring>

namespace
{
static char s_testId[64] = {};
static idProgram * s_program = nullptr;

const idEventDef * FindEvent(const char *) { return nullptr; }
bool ThreadResponds(const idEventDef &) { return false; }

void Cleanup(void *)
{
    s_program->FreeData();
    const bool cleared = s_program->NumStatements() == 0 && s_program->returnDef == nullptr;
    delete s_program;
    s_program = nullptr;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] SCRIPT CLEANUP %s %s\n", s_testId, cleared ? "PASS" : "FAIL");
}

void Compile(const char * text)
{
    idCompiler compiler(*s_program, FindEvent, ThreadResponds);
    compiler.CompileFile(text, "fixture.script", true, false);
}
}

int main()
{
    SifInitRpc(0);
    char mode[64] = {};
    FILE * input = std::fopen("host:script-case.txt", "rb");
    if (input == nullptr || std::fscanf(input, "%63s %63s", s_testId, mode) != 2)
    {
        ps2::FatalError("script probe requires an authored case manifest");
    }
    std::fclose(input);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] SCRIPT BEGIN %s %s\n", s_testId, mode);
    ps2::core::Init();
    const ps2::heap::Stats baseline = ps2::heap::GetTotalStats();
    s_program = new idProgram;
    {
        ps2::script::ErrorScope cleanup(Cleanup, nullptr);
        s_program->BeginCompilation();
        if (std::strcmp(mode, "valid") == 0)
        {
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                s_program->BeginCompilation();
                Compile("float answer = 42; vector v = '1 2 3'; float test(float x) { if (x > 0) { return x + answer; } return 0; }");
                const function_t * function = s_program->FindFunction("test");
                idVarDef * value = s_program->GetDef(&type_float, "answer", &def_namespace);
                if (function == nullptr || function->numStatements == 0 || value == nullptr || *value->value.floatPtr != 42.0f)
                {
                    ps2::FatalError("compiler fixture produced incorrect definitions");
                }
                s_program->FreeData();
                if (s_program->NumStatements() != 0 || ps2::script::ErrorScope::IsActive() == false)
                {
                    ps2::FatalError("compiler fixture did not reset its program");
                }
            }
        }
        else if (std::strcmp(mode, "include") == 0)
        {
            Compile("#include \"included.script\"\nfloat test() { return answer; }");
            idVarDef * value = s_program->GetDef(&type_float, "answer", &def_namespace);
            if (value == nullptr || *value->value.floatPtr != 42.0f) { ps2::FatalError("included fixture missing its definition"); }
        }
        else if (std::strcmp(mode, "include-missing") == 0) { Compile("#include \"absent.script\"\n"); }
        else if (std::strcmp(mode, "syntax") == 0) { Compile("float test() {\n return unknown;\n}\n"); }
        else if (std::strcmp(mode, "eof") == 0) { Compile("void test() {\n"); }
        else if (std::strcmp(mode, "lexical") == 0) { Compile("string x = \"unterminated\n"); }
        else if (std::strcmp(mode, "vector") == 0) { Compile("vector x = '1 nope 3';"); }
        else if (std::strcmp(mode, "divide") == 0) { Compile("float test() { return 3 / 0; }"); }
        else if (std::strcmp(mode, "remainder") == 0) { Compile("float test() { return 3 % 0; }"); }
        else if (std::strcmp(mode, "type") == 0) { Compile("float x; string x;"); }
        else if (std::strcmp(mode, "event") == 0) { Compile("scriptEvent void absent();"); }
        else if (std::strcmp(mode, "depth") == 0)
        {
            idStr text = "float test() { return ";
            for (int index = 0; index < 80; ++index) { text += "("; }
            text += "1";
            for (int index = 0; index < 80; ++index) { text += ")"; }
            text += "; }";
            Compile(text.c_str());
        }
        else if (std::strcmp(mode, "globals") == 0)
        {
            ps2::script::ErrorScope::SetLocation("globals.fixture", 1);
            for (int index = 0; index <= MAX_GLOBALS / MAX_STRING_LEN; ++index)
            {
                s_program->AllocDef(&type_string, "s", &def_namespace, false);
            }
        }
        else if (std::strcmp(mode, "statements") == 0)
        {
            ps2::script::ErrorScope::SetLocation("statements.fixture", 1);
            for (int index = 0; index < MAX_STATEMENTS; ++index) { s_program->AllocStatement(); }
        }
        else if (std::strcmp(mode, "functions") == 0)
        {
            ps2::script::ErrorScope::SetLocation("functions.fixture", 1);
            idVarDef * def = s_program->AllocDef(&type_function, "f", &def_namespace, true);
            for (int index = 0; index <= MAX_FUNCS; ++index) { s_program->AllocFunction(def); }
        }
        else { ps2::FatalError("unknown script fixture: %s", mode); }
    }
    delete s_program;
    s_program = nullptr;
    const ps2::heap::Stats after = ps2::heap::GetTotalStats();
    const bool recovered = baseline.requestedBytes == after.requestedBytes &&
        baseline.backingBytes == after.backingBytes && baseline.allocationCount == after.allocationCount;
    ps2::core::Shutdown();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] SCRIPT RESULT %s %s\n", s_testId, recovered ? "PASS" : "FAIL");
    while (true) { SleepThread(); }
}
