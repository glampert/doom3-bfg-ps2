// ================================================================================================
// File: swf_runtime_tests.cpp
// Brief: Prove scoped script budgets fail in release and lifetime accounting spans separate ticks.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include "ps2/ui/swf_runtime.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace ps2
{
[[noreturn]] void FatalError(const char *, ...)
{
    _exit(42);
}

void Log(LogLevel, const char *, ...)
{
}
} // namespace ps2

namespace
{
namespace runtime = ps2::ui::swf;

struct Sprite
{
    const Sprite * parent;
};

static const void * Parent(const void * origin)
{
    return static_cast<const Sprite *>(origin)->parent;
}

static void Origins(unsigned depth)
{
    const Sprite root{ nullptr };
    runtime::OriginScope origin(&root, nullptr, Parent);

    if (depth != 0)
    {
        Origins(depth - 1);
    }
}

static void Calls(unsigned depth)
{
    runtime::CallScope call;

    if (depth != 0)
    {
        Calls(depth - 1);
    }
}

static void Visits(unsigned depth)
{
    runtime::VisitScope visit;

    if (depth != 0)
    {
        Visits(depth - 1);
    }
}

static bool Rejects(unsigned mode)
{
    const pid_t child = fork();

    if (child == 0)
    {
        runtime::ExecutionScope scope("host-budget-check");

        switch (mode)
        {
        case 0:
            for (unsigned index = 0; index <= 65536; ++index)
            {
                runtime::ChargeInstruction();
            }

            break;
        case 1:
            for (unsigned index = 0; index <= 32768; ++index)
            {
                runtime::ChargeCommand();
            }

            break;
        case 2:
            Calls(32);
            break;
        case 3:
            Visits(64);
            break;
        case 4:
            runtime::CheckStack(-1);
            break;
        case 5:
            runtime::CheckStack(4097);
            break;
        case 6:
            runtime::CheckString(4097);
            break;
        case 7:
            runtime::CheckCount("native argument count", 17, 16);
            break;
        case 8:
            for (unsigned index = 0; index <= 8192; ++index)
            {
                runtime::TrackObject(true);
            }

            break;
        case 9:
            for (unsigned index = 0; index <= 8192; ++index)
            {
                runtime::TrackFunction(true);
            }

            break;
        case 10:
            for (unsigned index = 0; index <= 512; ++index)
            {
                runtime::TrackString(true, 4096);
            }

            break;
        case 11:
            runtime::TrackProperties(65537);
            break;
        case 12:
            for (unsigned index = 0; index <= 512; ++index)
            {
                char name[32];
                std::snprintf(name, sizeof(name), "dependency_%u", index);
                runtime::Trace("host", name);
            }

            break;
        case 13:
        {
            runtime::ExecutionScope nested("invalid-nested-owner");
            break;
        }
        case 14:
            runtime::ResetStats();
            break;
        case 15:
        {
            const Sprite root{ nullptr };
            const Sprite childSprite{ &root };
            runtime::OriginScope origin(&childSprite, nullptr, Parent);
            runtime::CheckSeek(&root, "direct source ancestor");
            break;
        }
        case 16:
        {
            const Sprite root{ nullptr };
            const Sprite childSprite{ &root };
            runtime::OriginScope origin(&childSprite, &root, Parent);
            runtime::CheckSeek(&root, "borrowed root receiver cannot hide the script source");
            break;
        }
        case 17:
        {
            const Sprite root{ nullptr };
            const Sprite item{ &root };
            const Sprite childSprite{ &item };
            runtime::OriginScope outer(&childSprite, nullptr, Parent);
            runtime::OriginScope inner(&root, &root, Parent);
            runtime::CheckSeek(&item, "inner root callback cannot retire an outer child script");
            break;
        }
        case 18:
        {
            const Sprite root{ nullptr };
            const Sprite childSprite{ &root };
            runtime::OriginScope origin(&root, &childSprite, Parent);
            runtime::CheckSeek(&root, "receiver ancestors remain protected independently of the source");
            break;
        }
        case 19:
            Origins(32);
            break;
        case 20:
        {
            Sprite cycle{ nullptr };
            cycle.parent = &cycle;
            runtime::OriginScope origin(&cycle, nullptr, Parent);
            break;
        }
        case 21:
        {
            runtime::OriginScope origin(nullptr, nullptr, Parent);
            break;
        }
        default:
            _exit(1);
        }

        _exit(0);
    }

    if (child < 0)
    {
        return false;
    }

    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42;
}
} // namespace

int main()
{
    // The normal authored/inactive path preserves its old unchecked behavior.
    runtime::CheckCount("outside scope", -1, 0);
    runtime::CheckString(8192);
    runtime::Require(false, "outside scope");
    runtime::CheckSeek(nullptr, "outside scope");

    {
        runtime::OriginScope unchecked(nullptr, nullptr, nullptr);
    }

    runtime::TrackObject(true);
    runtime::TrackFunction(true);
    runtime::TrackString(true, 4);
    runtime::TrackProperties(2);
    runtime::ResetStats();

    {
        runtime::ExecutionScope scope("host-positive-check");
        runtime::ChargeInstruction();
        runtime::ChargeCommand();
        runtime::CheckStack(4096);
        runtime::Trace("callback", "inhibitControl");
        runtime::Trace("callback", "inhibitControl");
        runtime::TrackObject(true);
        runtime::TrackFunction(true);
        runtime::TrackString(true, 8);
        runtime::TrackProperties(3);
        Calls(2);
        Visits(3);
    }

    // Cleanup outside the scope must release live charges acquired by earlier ticks.
    runtime::TrackObject(false);
    runtime::TrackFunction(false);
    runtime::TrackString(false, 8);
    runtime::TrackProperties(-3);

    {
        runtime::ExecutionScope scope("host-positive-check");
        runtime::ChargeInstruction();
        runtime::ChargeInstruction();
        runtime::ChargeCommand();
    }

    const auto stats = runtime::GetStats();
    bool passed = stats.scopes == 2 && stats.instructions == 3 && stats.commands == 2 &&
                  stats.calls == 3 && stats.allocations == 3 && stats.traces == 1 &&
                  stats.peakInstructions == 2 && stats.peakCommands == 1 && stats.peakCalls == 3 &&
                  stats.peakDepth == 3 && stats.peakVisitDepth == 4 && stats.peakStack == 4096 &&
                  stats.peakObjects == 2 && stats.peakFunctions == 2 && stats.peakProperties == 5 &&
                  stats.peakStrings == 2 && stats.peakStringBytes == 12;
    runtime::TrackObject(false);
    runtime::TrackFunction(false);
    runtime::TrackString(false, 4);
    runtime::TrackProperties(-2);
    runtime::ResetStats();

    {
        const Sprite root{ nullptr };
        const Sprite childSprite{ &root };
        const Sprite sibling{ &root };
        runtime::ExecutionScope scope("host-origin-positive-check");

        {
            runtime::OriginScope outer(&childSprite, nullptr, Parent);
            runtime::CheckSeek(&childSprite, "a source may seek its own timeline");
            runtime::CheckSeek(&sibling, "sibling seeks do not retire the source");

            {
                runtime::OriginScope inner(&sibling, nullptr, Parent);
                runtime::CheckSeek(&childSprite, "unrelated inner calls preserve safe outer self-seeks");
            }

            runtime::CheckSeek(&sibling, "returning from an inner call clears its origin frame");
        }

        runtime::CheckSeek(&root, "returning from all calls clears protected ancestors");
        Origins(31);
    }

    for (unsigned mode = 0; mode < 22; ++mode)
    {
        if (!Rejects(mode))
        {
            std::fprintf(stderr, "SWF runtime guard failed case %u\n", mode);
            passed = false;
        }
    }

    std::puts(passed ? "SWF runtime budget checks passed." : "SWF runtime budget checks failed.");
    return passed ? 0 : 1;
}
