// ================================================================================================
// File: idlib_tests.cpp
// Brief: Catch scalar math, lexer, formatting and scheduler regressions on the EE and host.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "tests/smoketests/idlib_tests.h"
#include "ps2/system/heap.h"

// Upstream headers are a system include for this strict C++20 test translation unit.
#include <idlib/precompiled.h>

namespace ps2::smoketests
{
namespace
{

bool Check(const char * name, bool passed)
{
    Sys_Printf("[D3BFG] CHECK idlib/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool Near(float actual, float expected)
{
    constexpr float kTolerance = 0.0001f;
    return fabsf(actual - expected) < kTolerance;
}

bool CheckDrawVert()
{
    idDrawVert vertex;
    vertex.Clear();
    vertex.SetTexCoord(0.5f, -2.0f);
    return Check("drawvert-half-layout", sizeof(idDrawVert) == 32 &&
        vertex.GetTexCoordNativeS() == 0x3800U && vertex.GetTexCoordNativeT() == 0xC000U &&
        Near(F16toF32(vertex.GetTexCoordNativeS()), 0.5f) &&
        Near(F16toF32(vertex.GetTexCoordNativeT()), -2.0f) && F32toF16(0.0f) == 0);
}

bool CheckMatrix()
{
    // Scale, then translate: a different multiply order produces a visibly different point.
    const idRenderMatrix scale(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    const idRenderMatrix translation(
        1.0f, 0.0f, 0.0f, 5.0f,
        0.0f, 1.0f, 0.0f, 6.0f,
        0.0f, 0.0f, 1.0f, 7.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    idRenderMatrix composed;
    idRenderMatrix::Multiply(translation, scale, composed);
    const idVec3 input(1.0f, 2.0f, 3.0f);
    idVec4 output;
    composed.TransformPoint(input, output);
    return Check("matrix-compose", Near(output.x, 7.0f) && Near(output.y, 12.0f) &&
        Near(output.z, 19.0f) && Near(output.w, 1.0f));
}

bool CheckPolynomialLifetime()
{
    const auto tag = static_cast<std::uint16_t>(TAG_MATH);
    const ps2::heap::Stats initial = ps2::heap::GetStats(tag);
    bool passed = false;
    {
        const idPolynomial original(1.0f, 2.0f, 3.0f);
        idPolynomial copy(original);
        copy[0] = 7.0f;
        const idPolynomial sum = original + copy;
        idPolynomial assigned;
        assigned = sum;
        passed = Near(original.GetValue(0.0f), 3.0f) && Near(copy.GetValue(0.0f), 7.0f) &&
            Near(assigned.GetValue(0.0f), 10.0f);
    }
    const ps2::heap::Stats final = ps2::heap::GetStats(tag);
    passed = passed && final.requestedBytes == initial.requestedBytes &&
        final.backingBytes == initial.backingBytes && final.allocationCount == initial.allocationCount;
    return Check("polynomial-copy-lifetime", passed);
}

bool CheckTaggedAlignment()
{
    struct alignas(128) AlignedValue
    {
        int value;
    };
    const auto tag = static_cast<std::uint16_t>(TAG_TEMP);
    const ps2::heap::Stats before = ps2::heap::GetStats(tag);
    auto * allocation = new (TAG_TEMP) AlignedValue{42};
    bool passed = reinterpret_cast<std::uintptr_t>(allocation) % 128 == 0 && allocation->value == 42 &&
        ps2::heap::GetStats(tag).requestedBytes == before.requestedBytes + sizeof(AlignedValue);
    delete allocation;
    const ps2::heap::Stats after = ps2::heap::GetStats(tag);
    passed = passed && after.requestedBytes == before.requestedBytes &&
        after.backingBytes == before.backingBytes && after.allocationCount == before.allocationCount;
    return Check("tagged-overaligned-new", passed);
}

bool CheckEmptySilhouette()
{
    idTraceModel empty;
    int edges[MAX_TRACEMODEL_EDGES] = {};
    const idVec3 direction(0.0f, 0.0f, 1.0f);
    return Check("empty-trace-silhouette", empty.GetParallelProjectionSilhouetteEdges(direction, edges) == 0);
}

bool CheckSurfaceRay()
{
    idDrawVert vertices[3];
    for (idDrawVert & vertex : vertices)
    {
        vertex.Clear();
    }
    vertices[1].xyz.x = 1.0f;
    vertices[2].xyz.y = 1.0f;
    const int indices[3] = {0, 1, 2};
    const idSurface surface(vertices, 3, indices, 3);
    const idVec3 above(0.25f, 0.25f, 1.0f);
    const idVec3 downward(0.0f, 0.0f, -1.0f);
    const idVec3 coplanar(0.25f, 0.25f, 0.0f);
    const idVec3 parallel(1.0f, 0.0f, 0.0f);
    float distance = 0.0f;
    bool passed = surface.RayIntersection(above, downward, distance) && Near(distance, 1.0f);
    passed = !surface.RayIntersection(coplanar, parallel, distance) && passed;
    return Check("surface-ray-parallel", passed);
}

bool CheckLexer()
{
    constexpr char kSource[] = "// fixture\nentity { key \"two words\" count 42 ratio 1.25 }";
    idLexer lexer;
    idToken token;
    bool passed = lexer.LoadMemory(kSource, static_cast<int>(sizeof(kSource) - 1), "idlib-smoke") != 0;
    passed = lexer.ExpectTokenString("entity") != 0 && passed;
    passed = lexer.ExpectTokenString("{") != 0 && passed;
    passed = lexer.ExpectTokenString("key") != 0 && passed;
    passed = lexer.ReadToken(&token) != 0 && token.type == TT_STRING &&
        token.Icmp("two words") == 0 && token.line == 2 && passed;
    passed = lexer.ExpectTokenString("count") != 0 && passed;
    passed = lexer.ReadToken(&token) != 0 && token.type == TT_NUMBER && token.GetIntValue() == 42 && passed;
    passed = lexer.ExpectTokenString("ratio") != 0 && passed;
    passed = lexer.ReadToken(&token) != 0 && token.type == TT_NUMBER && Near(token.GetFloatValue(), 1.25f) && passed;
    passed = lexer.ExpectTokenString("}") != 0 && passed;
    passed = lexer.ReadToken(&token) == 0 && passed;
    return Check("lexer-comments-strings-numbers", passed);
}

int Format(char * destination, int size, const char * format, ...)
{
    va_list args;
    va_start(args, format);
    const int length = idStr::vsnPrintf(destination, size, format, args);
    va_end(args);
    return length;
}

bool CheckStrings()
{
    char exact[5] = {};
    char truncated[5] = {};
    const int exactLength = Format(exact, static_cast<int>(sizeof(exact)), "%s", "abcd");
    const int truncatedLength = Format(truncated, static_cast<int>(sizeof(truncated)), "%s", "abcdef");
    const idStrStatic<16> integer(42);
    const idStrStatic<16> boolean(true);
    const idStrStatic<16> character('x');
    return Check("bounded-format-static-scalar", exactLength == 4 && idStr::Cmp(exact, "abcd") == 0 &&
        truncatedLength == -1 && truncated[4] == '\0' && idStr::Cmp(truncated, "abcd") == 0 &&
        integer.Icmp("42") == 0 && boolean.Icmp("1") == 0 && character.Icmp("x") == 0);
}

struct JobState
{
    int step;
    bool valid;
};

struct JobData
{
    JobState * state;
    int expectedStep;
};

void RunOrderedJob(void * data)
{
    auto * job = static_cast<JobData *>(data);
    job->state->valid = job->state->valid && job->state->step == job->expectedStep;
    ++job->state->step;
}

bool CheckPrimitives()
{
    mutexHandle_t mutex = 0;
    Sys_MutexCreate(mutex);
    bool passed = Sys_MutexLock(mutex, true) && Sys_MutexLock(mutex, false);
    Sys_MutexUnlock(mutex);
    Sys_MutexUnlock(mutex);
    Sys_MutexDestroy(mutex);
    passed = mutex == -1 && passed;

    signalHandle_t automatic = {};
    Sys_SignalCreate(automatic, false);
    passed = !Sys_SignalWait(automatic, 0) && passed;
    Sys_SignalRaise(automatic);
    passed = Sys_SignalWait(automatic, 0) && !Sys_SignalWait(automatic, 0) && passed;
    Sys_SignalDestroy(automatic);

    signalHandle_t manual = {};
    Sys_SignalCreate(manual, true);
    Sys_SignalRaise(manual);
    passed = Sys_SignalWait(manual, 0) && Sys_SignalWait(manual, 0) && passed;
    Sys_SignalClear(manual);
    passed = !Sys_SignalWait(manual, 0) && passed;
    Sys_SignalDestroy(manual);

    interlockedInt_t value = INT_MAX;
    passed = Sys_InterlockedIncrement(value) == INT_MIN && Sys_InterlockedDecrement(value) == INT_MAX && passed;
    value = 5;
    passed = Sys_InterlockedAdd(value, 3) == 8 && Sys_InterlockedSub(value, 2) == 6 && passed;
    passed = Sys_InterlockedExchange(value, 1) == 6 && value == 1 && passed;
    passed = Sys_InterlockedCompareExchange(value, 6, 7) == 1 && value == 1 && passed;
    passed = Sys_InterlockedCompareExchange(value, 1, 7) == 1 && value == 7 && passed;
    int first = 1;
    int second = 2;
    void * pointer = &first;
    passed = Sys_InterlockedExchangePointer(pointer, &second) == &first && pointer == &second && passed;
    passed = Sys_InterlockedCompareExchangePointer(pointer, &first, &first) == &second && pointer == &second && passed;
    passed = Sys_InterlockedCompareExchangePointer(pointer, &second, &first) == &second && pointer == &first && passed;
    return Check("single-thread-mutex-signal-atomics", passed);
}

bool CheckEvents()
{
    Sys_ClearEvents();
    constexpr int kQueueCapacity = 64;
    const auto tag = static_cast<std::uint16_t>(TAG_EVENTS);
    const ps2::heap::Stats initial = ps2::heap::GetStats(tag);
    for (int index = 0; index <= kQueueCapacity; ++index)
    {
        auto * payload = static_cast<int *>(Mem_Alloc(static_cast<int>(sizeof(int)), TAG_EVENTS));
        *payload = index;
        Sys_QueEvent(SE_CONSOLE, index, 0, static_cast<int>(sizeof(int)), payload, 0);
    }
    const ps2::heap::Stats queued = ps2::heap::GetStats(tag);
    bool passed = queued.allocationCount == initial.allocationCount + kQueueCapacity &&
        queued.requestedBytes == initial.requestedBytes + kQueueCapacity * sizeof(int);
    for (int expected = 1; expected <= kQueueCapacity; ++expected)
    {
        const sysEvent_t event = Sys_GetEvent();
        passed = event.evType == SE_CONSOLE && event.evValue == expected &&
            event.evPtrLength == static_cast<int>(sizeof(int)) && event.evPtr != nullptr && passed;
        if (event.evPtr != nullptr)
        {
            passed = *static_cast<int *>(event.evPtr) == expected && passed;
            Mem_Free(event.evPtr);
        }
    }
    passed = Sys_GetEvent().evType == SE_NONE && passed;
    void * discarded = Mem_Alloc(8, TAG_EVENTS);
    Sys_QueEvent(SE_CONSOLE, 0, 0, 8, discarded, 0);
    Sys_ClearEvents();
    const ps2::heap::Stats final = ps2::heap::GetStats(tag);
    passed = final.allocationCount == initial.allocationCount &&
        final.requestedBytes == initial.requestedBytes && final.backingBytes == initial.backingBytes && passed;
    return Check("event-overflow-transfer-clear-ledger", passed);
}

bool CheckJobs()
{
    RegisterJob(RunOrderedJob, "idlib-smoke-order");
    const int initialLists = parallelJobManager->GetNumJobLists();
    JobState state = {0, true};
    JobData jobs[4] = {{&state, 0}, {&state, 1}, {&state, 2}, {&state, 3}};
    idParallelJobList * predecessor = parallelJobManager->AllocJobList(
        JOBLIST_RENDERER_FRONTEND, JOBLIST_PRIORITY_MEDIUM, 2, 1, nullptr);
    idParallelJobList * dependent = parallelJobManager->AllocJobList(
        JOBLIST_RENDERER_BACKEND, JOBLIST_PRIORITY_MEDIUM, 2, 1, nullptr);

    predecessor->AddJob(RunOrderedJob, &jobs[0]);
    predecessor->InsertSyncPoint(SYNC_SIGNAL);
    predecessor->InsertSyncPoint(SYNC_SYNCHRONIZE);
    predecessor->AddJob(RunOrderedJob, &jobs[1]);
    predecessor->Submit(nullptr, JOBLIST_PARALLELISM_MAX_THREADS);

    dependent->AddJob(RunOrderedJob, &jobs[2]);
    dependent->InsertSyncPoint(SYNC_SIGNAL);
    dependent->InsertSyncPoint(SYNC_SYNCHRONIZE);
    dependent->AddJob(RunOrderedJob, &jobs[3]);
    dependent->Submit(predecessor, JOBLIST_PARALLELISM_MAX_CORES);
    dependent->Wait();

    bool passed = state.valid && state.step == 4 && parallelJobManager->GetNumProcessingUnits() == 0 &&
        predecessor->GetNumExecutedJobs() == 2 && predecessor->GetNumSyncs() == 1 &&
        dependent->GetNumExecutedJobs() == 2 && dependent->GetNumSyncs() == 1 && !dependent->IsSubmitted();
    // Reuse the completed list to verify empty submission completes without waiting on a worker.
    dependent->Submit();
    passed = dependent->TryWait() && dependent->GetNumExecutedJobs() == 0 && passed;
    parallelJobManager->FreeJobList(dependent);
    parallelJobManager->FreeJobList(predecessor);
    passed = parallelJobManager->GetNumJobLists() == initialLists && passed;
    return Check("synchronous-jobs-dependency-sync-reuse", passed);
}

} // namespace

bool RunIdlibTests()
{
    bool passed = CheckDrawVert();
    passed = CheckMatrix() && passed;
    passed = CheckPolynomialLifetime() && passed;
    passed = CheckTaggedAlignment() && passed;
    passed = CheckEmptySilhouette() && passed;
    passed = CheckSurfaceRay() && passed;
    passed = CheckLexer() && passed;
    passed = CheckStrings() && passed;
    passed = CheckPrimitives() && passed;
    passed = CheckEvents() && passed;
    passed = CheckJobs() && passed;
    return passed;
}

} // namespace ps2::smoketests
