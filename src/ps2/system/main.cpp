// ================================================================================================
// File: main.cpp
// Brief: Stage platform/core smoke boot and persist its result before the runner stops the emulator.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"

#include "tests/smoketests/platform_boot.h"

#if PS2_D3BFG_CORE_TESTS
#include "tests/smoketests/core_boot.h"
#endif

#include <kernel.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

namespace
{

static constexpr size_t kTestIdCapacity = 64;
static constexpr size_t kFixturePathCapacity = 192;

struct BootManifest
{
    char testId[kTestIdCapacity] = "manual";
    char fixturePath[kFixturePathCapacity] = "host:fixture.txt";
};

bool ReadLine(FILE * file, char * output, size_t capacity)
{
    if (fgets(output, static_cast<int>(capacity), file) == nullptr)
    {
        return false;
    }
    const size_t length = strlen(output);
    if (length == 0 || output[length - 1] != '\n')
    {
        return false;
    }
    output[length - 1] = '\0';
    return true;
}

bool ReadManifest(BootManifest & manifest)
{
    FILE * file = fopen("host:smoke.manifest", "rb");
    if (file == nullptr)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] MANIFEST absent; using manual smoke fixture\n");
        return true;
    }
    char magic[32] = {};
    const bool linesRead = ReadLine(file, magic, sizeof(magic)) &&
        ReadLine(file, manifest.testId, sizeof(manifest.testId)) &&
        ReadLine(file, manifest.fixturePath, sizeof(manifest.fixturePath));
    const bool atEnd = fgetc(file) == EOF && ferror(file) == 0;
    const bool closed = fclose(file) == 0;
    if (!linesRead || !atEnd || !closed || strcmp(magic, "D3BFG_SMOKE 1") != 0)
    {
        return false;
    }
    const size_t idLength = strlen(manifest.testId);
    if (idLength == 0)
    {
        return false;
    }
    for (size_t index = 0; index < idLength; ++index)
    {
        const char letter = manifest.testId[index];
        if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') || letter == '-' || letter == '_'))
        {
            return false;
        }
    }
    if (strncmp(manifest.fixturePath, "host:", 5) != 0 || manifest.fixturePath[5] == '\0')
    {
        return false;
    }
    for (const char * cursor = manifest.fixturePath; *cursor != '\0'; ++cursor)
    {
        if (*cursor < ' ' || *cursor == '"' || *cursor == '\\')
        {
            return false;
        }
    }
    return true;
}

bool WriteResult(const BootManifest & manifest, bool manifestPassed, bool platformPassed,
    const char * coreStatus)
{
    FILE * file = fopen("host:result.json", "wb");
    if (file == nullptr)
    {
        return false;
    }
    const int written = fprintf(file,
        "{\"schema\":1,\"test_id\":\"%s\",\"manifest\":\"%s\",\"platform\":\"%s\",\"core\":\"%s\"}\n",
        manifest.testId, manifestPassed ? "PASS" : "FAIL", platformPassed ? "PASS" : "FAIL", coreStatus);
    const bool closed = fclose(file) == 0;
    return written > 0 && closed;
}

} // namespace

int main()
{
    // Preserve the loader's IOP/host filesystem. Console device bring-up is a later stage.
    SifInitRpc(0);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] STAGE platform BEGIN\n");
    BootManifest manifest;
    const bool manifestPassed = ReadManifest(manifest);
    if (!manifestPassed)
    {
        // Result metadata must remain valid JSON even when an untrusted manifest is malformed.
        manifest = BootManifest{};
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK manifest FAIL\n");
    }
    const bool platformPassed = ps2::smoketests::RunPlatformTests() && manifestPassed;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] STAGE platform %s\n", platformPassed ? "PASS" : "FAIL");

    const char * coreStatus = "SKIP";
#if PS2_D3BFG_CORE_TESTS
    if (platformPassed)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] STAGE core BEGIN\n");
        coreStatus = ps2::smoketests::RunCoreTests(manifest.fixturePath) ? "PASS" : "FAIL";
    }
#endif
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] STAGE core %s\n", coreStatus);
    const bool resultWritten = WriteResult(manifest, manifestPassed, platformPassed, coreStatus);
    const bool passed = platformPassed && strcmp(coreStatus, "FAIL") != 0 && resultWritten;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] RESULT %s %s\n", manifest.testId, passed ? "PASS" : "FAIL");
    if (!resultWritten)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK result-file FAIL\n");
    }

    // Keep the completed test available for log/result capture. The watchdog owns termination.
    while (true)
    {
        SleepThread();
    }
}
