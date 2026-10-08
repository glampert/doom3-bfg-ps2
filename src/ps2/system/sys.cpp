// ================================================================================================
// File: sys.cpp
// Brief: Give the initial synchronous Doom core real EE time, diagnostics and owned event storage.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "ps2/system/heap.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

#if defined(ID_HOST_TEST)
#include <chrono>
#include <thread>
#else
#include <kernel.h>
#include <timer.h>
#endif

#include <idlib/precompiled.h>

namespace
{

constexpr size_t kEventCapacity = 64;
constexpr std::uint64_t kMicrosecondsPerSecond = 1000000;
#if !defined(ID_HOST_TEST)
constexpr unsigned int kBusTicksPerSecond = 147456000;
#endif
sysEvent_t s_events[kEventCapacity] = {};
size_t s_eventHead = 0;
size_t s_eventCount = 0;
std::uint64_t s_startTicks = 0;
bool s_initialized = false;

[[noreturn]] void Halt()
{
    std::fflush(stdout);
    std::fflush(stderr);
#if defined(ID_HOST_TEST)
    std::abort();
#else
    Exit(1);
#endif
}

std::uint64_t ReadTicks()
{
#if defined(ID_HOST_TEST)
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#else
    return GetTimerSystemTime();
#endif
}

int WrapAdd(int value, int increment)
{
    const std::uint32_t result = static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(increment);
    return std::bit_cast<int>(result);
}

int WrapSub(int value, int decrement)
{
    const std::uint32_t result = static_cast<std::uint32_t>(value) - static_cast<std::uint32_t>(decrement);
    return std::bit_cast<int>(result);
}

void FreeEvent(sysEvent_t & event)
{
    if (event.evPtr != nullptr)
    {
        Mem_Free(event.evPtr);
    }
    event = {};
}

class CoreSys final : public idSys
{
public:
    void DebugPrintf(const char * format, ...) override
    {
        va_list args;
        va_start(args, format);
        Sys_DebugVPrintf(format, args);
        va_end(args);
    }
    void DebugVPrintf(const char * format, va_list args) override { Sys_DebugVPrintf(format, args); }
    double GetClockTicks() override { return Sys_GetClockTicks(); }
    double ClockTicksPerSecond() override { return Sys_ClockTicksPerSecond(); }
    cpuid_t GetProcessorId() override { return Sys_GetProcessorId(); }
    const char * GetProcessorString() override { return Sys_GetProcessorString(); }
    const char * FPU_GetState() override { return Sys_FPU_GetState(); }
    bool FPU_StackIsEmpty() override { return Sys_FPU_StackIsEmpty(); }
    void FPU_SetFTZ(bool enabled) override { Sys_FPU_SetFTZ(enabled); }
    void FPU_SetDAZ(bool enabled) override { Sys_FPU_SetDAZ(enabled); }
    void FPU_EnableExceptions(int exceptions) override { Sys_FPU_EnableExceptions(exceptions); }
    bool LockMemory(void * pointer, int bytes) override { return Sys_LockMemory(pointer, bytes); }
    bool UnlockMemory(void * pointer, int bytes) override { return Sys_UnlockMemory(pointer, bytes); }
    void GetCallStack(address_t * stack, int size) override { Sys_GetCallStack(stack, size); }
    const char * GetCallStackStr(const address_t * stack, int size) override { return Sys_GetCallStackStr(stack, size); }
    const char * GetCallStackCurStr(int depth) override { return Sys_GetCallStackCurStr(depth); }
    void ShutdownSymbols() override { Sys_ShutdownSymbols(); }
    int DLL_Load(const char * name) override { return Sys_DLL_Load(name); }
    void * DLL_GetProcAddress(int handle, const char * name) override { return Sys_DLL_GetProcAddress(handle, name); }
    void DLL_Unload(int handle) override { Sys_DLL_Unload(handle); }
    void DLL_GetFileName(const char *, char *, int) override { Sys_Error("Dynamic libraries are unavailable in the resident core"); }
    sysEvent_t GenerateMouseButtonEvent(int button, bool down) override
    {
        if (button < 1 || button > 8)
        {
            Sys_Error("Mouse button is outside the engine key range");
        }
        sysEvent_t event = {};
        event.evType = SE_KEY;
        event.evValue = K_MOUSE1 + button - 1;
        event.evValue2 = down ? 1 : 0;
        return event;
    }
    sysEvent_t GenerateMouseMoveEvent(int x, int y) override
    {
        sysEvent_t event = {};
        event.evType = SE_MOUSE;
        event.evValue = x;
        event.evValue2 = y;
        return event;
    }
    void OpenURL(const char *, bool) override { Sys_Error("Opening URLs is unavailable in the core"); }
    void StartProcess(const char *, bool) override { Sys_Error("Starting processes is unavailable in the core"); }
};

CoreSys s_sys;

} // namespace

idSys * sys = &s_sys;

void Sys_Init()
{
    if (s_initialized)
    {
        Sys_Error("Sys_Init called twice");
    }
    s_startTicks = ReadTicks();
    s_initialized = true;
    Sys_ClearEvents();
}

void Sys_Shutdown()
{
    Sys_ClearEvents();
    s_initialized = false;
}

void Sys_Printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fflush(stdout);
}

void Sys_DebugVPrintf(const char * format, va_list args)
{
    std::vfprintf(stdout, format, args);
    std::fflush(stdout);
}

void Sys_DebugPrintf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    Sys_DebugVPrintf(format, args);
    va_end(args);
}

void Sys_Error(const char * format, ...)
{
    std::fputs("[D3BFG] FATAL sys: ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    Halt();
}

uint64 Sys_Microseconds()
{
    const std::uint64_t ticks = ReadTicks() - s_startTicks;
#if defined(ID_HOST_TEST)
    return static_cast<uint64>(ticks);
#else
    u32 seconds = 0;
    u32 microseconds = 0;
    TimerBusClock2USec(ticks, &seconds, &microseconds);
    return static_cast<uint64>(seconds) * kMicrosecondsPerSecond + microseconds;
#endif
}

int Sys_Milliseconds()
{
    // Preserve the engine's signed 32-bit timer wrap without an out-of-range conversion.
    const std::uint32_t milliseconds = static_cast<std::uint32_t>(Sys_Microseconds() / 1000U);
    return std::bit_cast<int>(milliseconds);
}

double Sys_GetClockTicks()
{
    // Upstream's profiling API explicitly requires double; frame time remains integer.
    return static_cast<double>(ReadTicks() - s_startTicks);
}

double Sys_ClockTicksPerSecond()
{
#if defined(ID_HOST_TEST)
    return static_cast<double>(kMicrosecondsPerSecond);
#else
    return static_cast<double>(kBusTicksPerSecond);
#endif
}

void Sys_Sleep(int milliseconds)
{
    if (milliseconds < 0)
    {
        Sys_Error("Negative sleep duration");
    }
#if defined(ID_HOST_TEST)
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else
    const uint64 start = Sys_Microseconds();
    const uint64 duration = static_cast<uint64>(milliseconds) * 1000U;
    while (Sys_Microseconds() - start < duration)
    {
        __asm__ volatile("" ::: "memory");
    }
#endif
}

cpuid_t Sys_GetProcessorId() { return CPUID_GENERIC; }
const char * Sys_GetProcessorString() { return CPUSTRING; }
void Sys_CPUCount(int & physical, int & logical, int & packages) { physical = logical = packages = 1; }

bool Sys_FPU_StackIsEmpty() { return true; } // COP1 has registers rather than an x87 stack.
void Sys_FPU_ClearStack() {}
const char * Sys_FPU_GetState() { return "scalar core: fixed EE precision and rounding"; }
void Sys_FPU_EnableExceptions(int exceptions)
{
    if (exceptions != 0) { Sys_Error("EE floating point exception traps are unavailable"); }
}
void Sys_FPU_SetPrecision(int precision)
{
    if (precision != FPU_PRECISION_SINGLE) { Sys_Error("EE floating point precision is fixed to single"); }
}
void Sys_FPU_SetRounding(int rounding)
{
    if (rounding != FPU_ROUNDING_TO_ZERO) { Sys_Error("EE floating point rounding is fixed toward zero"); }
}
void Sys_FPU_SetFTZ(bool enabled)
{
    if (!enabled) { Sys_Error("EE denormal flushing cannot be disabled"); }
}
void Sys_FPU_SetDAZ(bool enabled)
{
    if (!enabled) { Sys_Error("EE denormal operands cannot be enabled"); }
}

bool Sys_LockMemory(void * pointer, int bytes) { return bytes >= 0 && (pointer != nullptr || bytes == 0); }
bool Sys_UnlockMemory(void * pointer, int bytes) { return Sys_LockMemory(pointer, bytes); }

void Sys_GetCallStack(address_t * stack, int size)
{
    if (size < 0 || (stack == nullptr && size != 0)) { Sys_Error("Invalid call stack buffer"); }
    for (int index = 0; index < size; ++index) { stack[index] = 0; }
}
const char * Sys_GetCallStackStr(const address_t *, int) { return "call stack unavailable; symbolize ELF addresses offline"; }
const char * Sys_GetCallStackCurStr(int) { return "call stack unavailable; symbolize ELF addresses offline"; }
void Sys_ShutdownSymbols() {}
int Sys_DLL_Load(const char *) { return 0; }
void * Sys_DLL_GetProcAddress(int, const char *) { return nullptr; }
void Sys_DLL_Unload(int handle)
{
    if (handle != 0) { Sys_Error("Dynamic libraries are unavailable in the resident core"); }
}

uintptr_t Sys_GetCurrentThreadID() { return 1; }
uintptr_t Sys_CreateThread(xthread_t, void *, xthreadPriority, const char *, core_t, int, bool)
{
    Sys_Error("Worker threads are unavailable in the synchronous core");
}
void Sys_WaitForThread(uintptr_t handle)
{
    if (handle != 0) { Sys_Error("Waiting for a worker is unavailable in the synchronous core"); }
}
void Sys_DestroyThread(uintptr_t handle)
{
    if (handle != 0) { Sys_Error("Destroying a worker is unavailable in the synchronous core"); }
}
void Sys_SetCurrentThreadName(const char *) {}
void Sys_Yield() { __asm__ volatile("" ::: "memory"); }

void Sys_SignalCreate(signalHandle_t & handle, bool manualReset) { handle = {false, manualReset}; }
void Sys_SignalDestroy(signalHandle_t & handle) { handle = {}; }
void Sys_SignalRaise(signalHandle_t & handle) { handle.signaled = true; }
void Sys_SignalClear(signalHandle_t & handle) { handle.signaled = false; }
bool Sys_SignalWait(signalHandle_t & handle, int timeout)
{
    if (timeout < -1) { Sys_Error("Invalid signal timeout"); }
    if (!handle.signaled)
    {
        if (timeout == -1) { Sys_Error("An unsignaled infinite wait cannot progress on the sole core thread"); }
        if (timeout > 0) { Sys_Sleep(timeout); }
        if (!handle.signaled) { return false; }
    }
    if (!handle.manualReset) { handle.signaled = false; }
    return true;
}

void Sys_MutexCreate(mutexHandle_t & handle) { handle = 0; }
void Sys_MutexDestroy(mutexHandle_t & handle)
{
    if (handle != 0) { Sys_Error("Destroying an invalid or locked mutex"); }
    handle = -1;
}
bool Sys_MutexLock(mutexHandle_t & handle, bool)
{
    // Windows critical sections are recursive; the sole thread can always acquire its own lock.
    if (handle < 0 || handle == INT_MAX) { Sys_Error("Invalid mutex or recursive lock overflow"); }
    ++handle;
    return true;
}
void Sys_MutexUnlock(mutexHandle_t & handle)
{
    if (handle <= 0) { Sys_Error("Unlocking an invalid or unlocked mutex"); }
    --handle;
}

interlockedInt_t Sys_InterlockedIncrement(interlockedInt_t & value) { value = WrapAdd(value, 1); return value; }
interlockedInt_t Sys_InterlockedDecrement(interlockedInt_t & value) { value = WrapSub(value, 1); return value; }
interlockedInt_t Sys_InterlockedAdd(interlockedInt_t & value, interlockedInt_t increment) { value = WrapAdd(value, increment); return value; }
interlockedInt_t Sys_InterlockedSub(interlockedInt_t & value, interlockedInt_t decrement) { value = WrapSub(value, decrement); return value; }
interlockedInt_t Sys_InterlockedExchange(interlockedInt_t & value, interlockedInt_t replacement)
{
    const interlockedInt_t previous = value;
    value = replacement;
    return previous;
}
interlockedInt_t Sys_InterlockedCompareExchange(interlockedInt_t & value, interlockedInt_t comparand, interlockedInt_t replacement)
{
    const interlockedInt_t previous = value;
    if (value == comparand) { value = replacement; }
    return previous;
}
void * Sys_InterlockedExchangePointer(void * & pointer, void * replacement)
{
    void * previous = pointer;
    pointer = replacement;
    return previous;
}
void * Sys_InterlockedCompareExchangePointer(void * & pointer, void * comparand, void * replacement)
{
    void * previous = pointer;
    if (pointer == comparand) { pointer = replacement; }
    return previous;
}

void Sys_FlushCacheLine(const void * pointer, int offset)
{
#if defined(ID_HOST_TEST)
    (void)pointer;
    (void)offset;
#else
    if (pointer == nullptr || offset < 0) { Sys_Error("Invalid cache flush address"); }
    constexpr uintptr_t kLineMask = 63;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t relative = static_cast<uintptr_t>(offset);
    if (address > UINTPTR_MAX - relative - 64U) { Sys_Error("Cache flush address overflow"); }
    const uintptr_t start = (address + relative) & ~kLineMask;
    SyncDCache(reinterpret_cast<void *>(start), reinterpret_cast<void *>(start + 64U));
#endif
}

void Sys_QueEvent(sysEventType_t type, int value, int value2, int ptrLength, void * pointer, int device)
{
    if (type <= SE_NONE || type > SE_CONSOLE || ptrLength < 0 || (pointer == nullptr && ptrLength != 0))
    {
        Sys_Error("Invalid queued event");
    }
    if (s_eventCount == kEventCapacity)
    {
        // Queue ownership includes the displaced payload; dequeue transfers it to the caller.
        FreeEvent(s_events[s_eventHead]);
        s_eventHead = (s_eventHead + 1) % kEventCapacity;
        --s_eventCount;
        Sys_Printf("[D3BFG] EVENT queue overflow: dropped oldest event\n");
    }
    const size_t tail = (s_eventHead + s_eventCount) % kEventCapacity;
    s_events[tail] = {type, value, value2, ptrLength, pointer, device};
    ++s_eventCount;
}

sysEvent_t Sys_GetEvent()
{
    if (s_eventCount == 0) { return {}; }
    const sysEvent_t event = s_events[s_eventHead];
    s_events[s_eventHead] = {};
    s_eventHead = (s_eventHead + 1) % kEventCapacity;
    --s_eventCount;
    return event;
}

void Sys_ClearEvents()
{
    while (s_eventCount != 0)
    {
        FreeEvent(s_events[s_eventHead]);
        s_eventHead = (s_eventHead + 1) % kEventCapacity;
        --s_eventCount;
    }
    s_eventHead = 0;
}
void Sys_GenerateEvents() {} // No input drivers run in the initial headless core.

ID_TIME_T Sys_FileTimeStamp(idFileHandle file)
{
    if (file == nullptr) { return -1; }
    struct stat status = {};
    const int descriptor = fileno(file);
    if (descriptor < 0 || fstat(descriptor, &status) != 0) { return -1; }
    return static_cast<ID_TIME_T>(status.st_mtime);
}

const char * Sys_DefaultBasePath() { return "host:"; }
const char * Sys_DefaultSavePath() { return "host:"; }
