// ================================================================================================
// File: resident_boot.cpp
// Brief: Boot and tick an explicitly selected authored game fixture, then persist its matched result.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/headless_fixture.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "tests/smoketests/game_boot.h"
#include <idlib/precompiled.h>
#include <kernel.h>
#include <sifrpc.h>

namespace
{
static char s_testId[64] = {};
static char s_mode[32] = {};

void Manifest()
{
    FILE * file = fopen("host:game.manifest", "rb");
    char magic[32] = {};
    const bool read = file != nullptr && fscanf(file, "%31s %63s %31s", magic, s_testId, s_mode) == 3;
    const bool closed = file != nullptr && fclose(file) == 0;
    if (!read || !closed || strcmp(magic, "D3BFG_GAME_1") != 0)
    {
        ps2::FatalError("resident game entry requires an authored fixture manifest");
    }
    for (const char * cursor = s_testId; *cursor != '\0'; ++cursor)
    {
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '_'))
        {
            ps2::FatalError("invalid game fixture run identity");
        }
    }
    if (strcmp(s_mode, "game") != 0 && strcmp(s_mode, "game-missing-map") != 0 && strcmp(s_mode, "game-syntax") != 0 &&
        strcmp(s_mode, "game-geometry") != 0 && strcmp(s_mode, "game-material") != 0 &&
        strcmp(s_mode, "game-player-args") != 0 && strcmp(s_mode, "game-player-script") != 0 &&
        strcmp(s_mode, "game-player-command") != 0)
    {
        ps2::FatalError("invalid game fixture mode");
    }
}
} // namespace

int main()
{
    SifInitRpc(0);
    Manifest();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] RUN %s BEGIN\n[D3BFG] STAGE platform PASS\n[D3BFG] STAGE core SKIP\n[D3BFG] STAGE game BEGIN\n", s_testId);
    const auto baseline = ps2::heap::GetTotalStats();
    for (unsigned int tag = 0; tag < TAG_NUM_TAGS; ++tag)
    {
        const auto stats = ps2::heap::GetStats(static_cast<std::uint16_t>(tag));
        if (stats.allocationCount != 0)
        {
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_TAG before %u %zu/%zu/%zu\n", tag, stats.requestedBytes, stats.backingBytes, stats.allocationCount);
        }
    }
    ps2::gamefixture::Enable();
    common->Init(0, nullptr, nullptr);
    cvarSystem->SetCVarBool("g_xp_bind_run_once", true);
    cvarSystem->SetCVarBool("s_noSound", true);
    declManager->Init();
    bool passed = ps2::smoketests::RunGameTests(s_mode);
    declManager->Shutdown();
    common->Shutdown();
    const auto final = ps2::heap::GetTotalStats();
    for (unsigned int tag = 0; tag < TAG_NUM_TAGS; ++tag)
    {
        const auto stats = ps2::heap::GetStats(static_cast<std::uint16_t>(tag));
        if (stats.allocationCount != 0)
        {
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_TAG after %u %zu/%zu/%zu\n", tag, stats.requestedBytes, stats.backingBytes, stats.allocationCount);
        }
    }
    const bool recovered = baseline.requestedBytes == final.requestedBytes &&
                           baseline.backingBytes == final.backingBytes && baseline.allocationCount == final.allocationCount;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK game/full-shutdown-ledger %s\n", recovered ? "PASS" : "FAIL");
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_FINAL before=%zu/%zu/%zu after=%zu/%zu/%zu\n",
             baseline.requestedBytes, baseline.backingBytes, baseline.allocationCount,
             final.requestedBytes, final.backingBytes, final.allocationCount);
    passed = recovered && passed;
    const char * status = passed ? "PASS" : "FAIL";
    FILE * file = fopen("host:result.json", "wb");
    if (file == nullptr)
    {
        ps2::FatalError("game result file could not open");
    }
    const int written = fprintf(file, "{\"schema\":1,\"test_id\":\"%s\",\"manifest\":\"PASS\",\"platform\":\"PASS\",\"core\":\"SKIP\",\"game\":\"%s\"}\n", s_testId, status);
    const bool closed = fclose(file) == 0;
    if (written <= 0 || !closed)
    {
        ps2::FatalError("game result file could not close");
    }
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] STAGE game %s\n[D3BFG] RESULT %s %s\n", status, s_testId, status);
    while (true)
    {
        SleepThread();
    }
}
