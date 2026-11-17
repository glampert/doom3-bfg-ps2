// ================================================================================================
// File: sys.cpp
// Brief: Give the initial synchronous Doom core real EE time, diagnostics and owned event storage,
// portable language/time metadata and explicit failures for unavailable OS services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#if defined(ID_HOST_TEST)
    #include <chrono>
    #include <thread>
#else // ID_HOST_TEST
    #include <kernel.h>
    #include <timer.h>
#endif // ID_HOST_TEST

#include <idlib/precompiled.h>

namespace
{

static constexpr size_t kEventCapacity = 64;
static constexpr std::uint64_t kMicrosecondsPerSecond = 1000000;

#if !defined(ID_HOST_TEST)
static constexpr unsigned int kBusTicksPerSecond = 147456000;
#endif // ID_HOST_TEST

static sysEvent_t s_events[kEventCapacity] = {};
static size_t s_eventHead = 0;
static size_t s_eventCount = 0;
static std::uint64_t s_startTicks = 0;
static bool s_initialized = false;

inline std::uint64_t ReadTicks()
{
#if defined(ID_HOST_TEST)
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
#else // ID_HOST_TEST
    return GetTimerSystemTime();
#endif // ID_HOST_TEST
}

inline int WrapAdd(int value, int increment)
{
    const std::uint32_t result = static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(increment);
    return std::bit_cast<int>(result);
}

inline int WrapSub(int value, int decrement)
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
    PS2_COLD_FUNC void DebugPrintf(const char * format, ...) override
    {
        va_list args;
        va_start(args, format);
        Sys_DebugVPrintf(format, args);
        va_end(args);
    }

    PS2_COLD_FUNC void DebugVPrintf(const char * format, va_list args) override
    {
        Sys_DebugVPrintf(format, args);
    }

    double GetClockTicks() override
    {
        return Sys_GetClockTicks();
    }

    double ClockTicksPerSecond() override
    {
        return Sys_ClockTicksPerSecond();
    }

    cpuid_t GetProcessorId() override
    {
        return Sys_GetProcessorId();
    }

    const char * GetProcessorString() override
    {
        return Sys_GetProcessorString();
    }

    const char * FPU_GetState() override
    {
        return Sys_FPU_GetState();
    }

    bool FPU_StackIsEmpty() override
    {
        return Sys_FPU_StackIsEmpty();
    }

    void FPU_SetFTZ(bool enabled) override
    {
        Sys_FPU_SetFTZ(enabled);
    }

    void FPU_SetDAZ(bool enabled) override
    {
        Sys_FPU_SetDAZ(enabled);
    }

    void FPU_EnableExceptions(int exceptions) override
    {
        Sys_FPU_EnableExceptions(exceptions);
    }

    bool LockMemory(void * pointer, int bytes) override
    {
        return Sys_LockMemory(pointer, bytes);
    }

    bool UnlockMemory(void * pointer, int bytes) override
    {
        return Sys_UnlockMemory(pointer, bytes);
    }

    void GetCallStack(address_t * stack, int size) override
    {
        Sys_GetCallStack(stack, size);
    }

    const char * GetCallStackStr(const address_t * stack, int size) override
    {
        return Sys_GetCallStackStr(stack, size);
    }

    const char * GetCallStackCurStr(int depth) override
    {
        return Sys_GetCallStackCurStr(depth);
    }

    void ShutdownSymbols() override
    {
        Sys_ShutdownSymbols();
    }

    int DLL_Load(const char * name) override
    {
        return Sys_DLL_Load(name);
    }

    void * DLL_GetProcAddress(int handle, const char * name) override
    {
        return Sys_DLL_GetProcAddress(handle, name);
    }

    void DLL_Unload(int handle) override
    {
        Sys_DLL_Unload(handle);
    }

    void DLL_GetFileName(const char *, char *, int) override
    {
        Sys_Error("Dynamic libraries are unavailable in the resident core");
    }

    sysEvent_t GenerateMouseButtonEvent(int button, bool down) override
    {
        if (button < 1 || button > 8) [[unlikely]]
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

    void OpenURL(const char *, bool) override
    {
        Sys_Error("Opening URLs is unavailable in the core");
    }

    void StartProcess(const char *, bool) override
    {
        Sys_Error("Starting processes is unavailable in the core");
    }
};

static CoreSys s_sys;

} // namespace

idSys * sys = &s_sys;

PS2_COLD_FUNC void Sys_Init()
{
    if (s_initialized) [[unlikely]]
    {
        Sys_Error("Sys_Init called twice");
    }

    s_startTicks = ReadTicks();
    s_initialized = true;
    Sys_ClearEvents();
}

PS2_COLD_FUNC void Sys_Shutdown()
{
    Sys_ClearEvents();
    s_initialized = false;
}

PS2_COLD_FUNC void Sys_Printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    ps2::LogV(ps2::LogLevel::Info, format, args);
    va_end(args);
}

PS2_COLD_FUNC void Sys_DebugVPrintf(const char * format, va_list args)
{
    ps2::LogV(ps2::LogLevel::Info, format, args);
}

PS2_COLD_FUNC void Sys_DebugPrintf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    Sys_DebugVPrintf(format, args);
    va_end(args);
}

PS2_COLD_FUNC void Sys_Error(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    ps2::FatalErrorV(format, args);
}

uint64 Sys_Microseconds()
{
    const std::uint64_t ticks = ReadTicks() - s_startTicks;

#if defined(ID_HOST_TEST)
    return static_cast<uint64>(ticks);
#else // ID_HOST_TEST
    u32 seconds = 0;
    u32 microseconds = 0;
    TimerBusClock2USec(ticks, &seconds, &microseconds);
    return static_cast<uint64>(seconds) * kMicrosecondsPerSecond + microseconds;
#endif // ID_HOST_TEST
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
#else // ID_HOST_TEST
    return static_cast<double>(kBusTicksPerSecond);
#endif // ID_HOST_TEST
}

void Sys_Sleep(int milliseconds)
{
    if (milliseconds < 0) [[unlikely]]
    {
        Sys_Error("Negative sleep duration");
    }

#if defined(ID_HOST_TEST)
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else // ID_HOST_TEST
    const uint64 start = Sys_Microseconds();
    const uint64 duration = static_cast<uint64>(milliseconds) * 1000U;
    while (Sys_Microseconds() - start < duration)
    {
        asm volatile("" ::: "memory");
    }
#endif // ID_HOST_TEST
}

cpuid_t Sys_GetProcessorId()
{
    return CPUID_GENERIC;
}

const char * Sys_GetProcessorString()
{
    return CPUSTRING;
}

void Sys_CPUCount(int & physical, int & logical, int & packages)
{
    physical = logical = packages = 1;
}

bool Sys_FPU_StackIsEmpty()
{
    return true;
}

void Sys_FPU_ClearStack()
{
}

const char * Sys_FPU_GetState()
{
    return "scalar core: fixed EE precision and rounding";
}

PS2_COLD_FUNC void Sys_FPU_EnableExceptions(int exceptions)
{
    if (exceptions != 0) [[unlikely]]
    {
        Sys_Error("EE floating point exception traps are unavailable");
    }
}

void Sys_FPU_SetPrecision(int precision)
{
    if (precision != FPU_PRECISION_SINGLE) [[unlikely]]
    {
        Sys_Error("EE floating point precision is fixed to single");
    }
}

void Sys_FPU_SetRounding(int rounding)
{
    if (rounding != FPU_ROUNDING_TO_ZERO) [[unlikely]]
    {
        Sys_Error("EE floating point rounding is fixed toward zero");
    }
}

void Sys_FPU_SetFTZ(bool enabled)
{
    if (!enabled) [[unlikely]]
    {
        Sys_Error("EE denormal flushing cannot be disabled");
    }
}

void Sys_FPU_SetDAZ(bool enabled)
{
    if (!enabled) [[unlikely]]
    {
        Sys_Error("EE denormal operands cannot be enabled");
    }
}

bool Sys_LockMemory(void * pointer, int bytes)
{
    return bytes >= 0 && (pointer != nullptr || bytes == 0);
}

bool Sys_UnlockMemory(void * pointer, int bytes)
{
    return Sys_LockMemory(pointer, bytes);
}

void Sys_GetCallStack(address_t * stack, int size)
{
    if (size < 0 || (stack == nullptr && size != 0)) [[unlikely]]
    {
        Sys_Error("Invalid call stack buffer");
    }

    // TODO: implement stack trace.
    for (int index = 0; index < size; ++index)
    {
        stack[index] = 0;
    }
}

const char * Sys_GetCallStackStr(const address_t *, int)
{
    return "call stack unavailable; symbolize ELF addresses offline";
}

const char * Sys_GetCallStackCurStr(int)
{
    return "call stack unavailable; symbolize ELF addresses offline";
}

PS2_COLD_FUNC void Sys_ShutdownSymbols()
{
}

PS2_COLD_FUNC int Sys_DLL_Load(const char *)
{
    return 0;
}

void * Sys_DLL_GetProcAddress(int, const char *)
{
    return nullptr;
}

PS2_COLD_FUNC void Sys_DLL_Unload(int handle)
{
    if (handle != 0) [[unlikely]]
    {
        Sys_Error("Dynamic libraries are unavailable in the resident core");
    }
}

uintptr_t Sys_GetCurrentThreadID()
{
    return 1;
}

PS2_COLD_FUNC uintptr_t Sys_CreateThread(xthread_t, void *, xthreadPriority, const char *, core_t, int, bool)
{
    Sys_Error("Worker threads are unavailable in the synchronous core");
}

void Sys_WaitForThread(uintptr_t handle)
{
    if (handle != 0) [[unlikely]]
    {
        Sys_Error("Waiting for a worker is unavailable in the synchronous core");
    }
}

PS2_COLD_FUNC void Sys_DestroyThread(uintptr_t handle)
{
    if (handle != 0) [[unlikely]]
    {
        Sys_Error("Destroying a worker is unavailable in the synchronous core");
    }
}

void Sys_SetCurrentThreadName(const char *)
{
}

void Sys_Yield()
{
    asm volatile("" ::: "memory");
}

void Sys_SignalCreate(signalHandle_t & handle, bool manualReset)
{
    handle = { false, manualReset };
}

void Sys_SignalDestroy(signalHandle_t & handle)
{
    handle = {};
}

void Sys_SignalRaise(signalHandle_t & handle)
{
    handle.signaled = true;
}

void Sys_SignalClear(signalHandle_t & handle)
{
    handle.signaled = false;
}

bool Sys_SignalWait(signalHandle_t & handle, int timeout)
{
    if (timeout < -1) [[unlikely]]
    {
        Sys_Error("Invalid signal timeout");
    }

    if (!handle.signaled)
    {
        if (timeout == -1) [[unlikely]]
        {
            Sys_Error("An unsignaled infinite wait cannot progress on the sole core thread");
        }
        if (timeout > 0)
        {
            Sys_Sleep(timeout);
        }
        if (!handle.signaled)
        {
            return false;
        }
    }

    if (!handle.manualReset)
    {
        handle.signaled = false;
    }
    return true;
}

void Sys_MutexCreate(mutexHandle_t & handle)
{
    handle = 0;
}

void Sys_MutexDestroy(mutexHandle_t & handle)
{
    if (handle != 0) [[unlikely]]
    {
        Sys_Error("Destroying an invalid or locked mutex");
    }
    handle = -1;
}

bool Sys_MutexLock(mutexHandle_t & handle, bool)
{
    // Windows critical sections are recursive; the sole thread can always acquire its own lock.
    if (handle < 0 || handle == INT_MAX) [[unlikely]]
    {
        Sys_Error("Invalid mutex or recursive lock overflow");
    }
    ++handle;
    return true;
}

void Sys_MutexUnlock(mutexHandle_t & handle)
{
    if (handle <= 0) [[unlikely]]
    {
        Sys_Error("Unlocking an invalid or unlocked mutex");
    }
    --handle;
}

interlockedInt_t Sys_InterlockedIncrement(interlockedInt_t & value)
{
    value = WrapAdd(value, 1);
    return value;
}

interlockedInt_t Sys_InterlockedDecrement(interlockedInt_t & value)
{
    value = WrapSub(value, 1);
    return value;
}

interlockedInt_t Sys_InterlockedAdd(interlockedInt_t & value, interlockedInt_t increment)
{
    value = WrapAdd(value, increment);
    return value;
}

interlockedInt_t Sys_InterlockedSub(interlockedInt_t & value, interlockedInt_t decrement)
{
    value = WrapSub(value, decrement);
    return value;
}

interlockedInt_t Sys_InterlockedExchange(interlockedInt_t & value, interlockedInt_t replacement)
{
    const interlockedInt_t previous = value;
    value = replacement;
    return previous;
}

interlockedInt_t Sys_InterlockedCompareExchange(interlockedInt_t & value, interlockedInt_t comparand, interlockedInt_t replacement)
{
    const interlockedInt_t previous = value;
    if (value == comparand)
    {
        value = replacement;
    }
    return previous;
}

void * Sys_InterlockedExchangePointer(void *& pointer, void * replacement)
{
    void * previous = pointer;
    pointer = replacement;
    return previous;
}

void * Sys_InterlockedCompareExchangePointer(void *& pointer, void * comparand, void * replacement)
{
    void * previous = pointer;
    if (pointer == comparand)
    {
        pointer = replacement;
    }
    return previous;
}

void Sys_FlushCacheLine(const void * pointer, int offset)
{
#if defined(ID_HOST_TEST)
    (void)pointer;
    (void)offset;
#else // ID_HOST_TEST
    if (pointer == nullptr || offset < 0) [[unlikely]]
    {
        Sys_Error("Invalid cache flush address");
    }

    constexpr uintptr_t kLineMask = 63;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t relative = static_cast<uintptr_t>(offset);
    if (address > UINTPTR_MAX - relative - 64U) [[unlikely]]
    {
        Sys_Error("Cache flush address overflow");
    }

    const uintptr_t start = (address + relative) & ~kLineMask;
    SyncDCache(reinterpret_cast<void *>(start), reinterpret_cast<void *>(start + 64U));
#endif // ID_HOST_TEST
}

void Sys_QueEvent(sysEventType_t type, int value, int value2, int ptrLength, void * pointer, int device)
{
    if (type <= SE_NONE || type > SE_CONSOLE || ptrLength < 0 || (pointer == nullptr && ptrLength != 0)) [[unlikely]]
    {
        Sys_Error("Invalid queued event");
    }

    if (s_eventCount == kEventCapacity) [[unlikely]]
    {
        // Queue ownership includes the displaced payload; dequeue transfers it to the caller.
        FreeEvent(s_events[s_eventHead]);
        s_eventHead = (s_eventHead + 1) % kEventCapacity;
        --s_eventCount;
        Sys_Printf("[D3BFG] EVENT queue overflow: dropped oldest event\n");
    }

    const size_t tail = (s_eventHead + s_eventCount) % kEventCapacity;
    s_events[tail] = { type, value, value2, ptrLength, pointer, device };
    ++s_eventCount;
}

sysEvent_t Sys_GetEvent()
{
    if (s_eventCount == 0)
    {
        return {};
    }

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

namespace ps2::system::runtime
{

static void (*s_eventGenerator)() = nullptr;
static char s_packageRoot[fs::kPathCapacity] = "host:";

void SetEventGenerator(void (*generator)())
{
    s_eventGenerator = generator;
}

PS2_COLD_FUNC void SetPackageRoot(const char * root)
{
    if (root == nullptr || std::strlen(root) >= sizeof(s_packageRoot) || !fs::IsValidPath(root)) [[unlikely]]
    {
        FatalError("invalid package root: %s", root != nullptr ? root : "(null)");
    }

    std::memcpy(s_packageRoot, root, std::strlen(root) + 1);
}

const char * PackageRoot()
{
    return s_packageRoot;
}

PS2_COLD_FUNC void PackageFile(char * output, size_t capacity, const char * name)
{
    if (!fs::BuildPath(output, capacity, s_packageRoot, "", name)) [[unlikely]]
    {
        FatalError("package path too long: %s/%s", s_packageRoot, name);
    }
}

} // namespace ps2::system::runtime

void Sys_GenerateEvents()
{
    // Headless runs and the test images register no device poll.
    if (ps2::system::runtime::s_eventGenerator != nullptr)
    {
        ps2::system::runtime::s_eventGenerator();
    }
}

ID_TIME_T Sys_FileTimeStamp(idFileHandle file)
{
    if (file == nullptr)
    {
        return -1;
    }

    struct stat status = {};
    const int descriptor = fileno(file);
    if (descriptor < 0 || fstat(descriptor, &status) != 0)
    {
        return -1;
    }

    return static_cast<ID_TIME_T>(status.st_mtime);
}

// The package's root holds base/; saves have no storage of their own yet.
const char * Sys_DefaultBasePath()
{
    return ps2::system::runtime::PackageRoot();
}

const char * Sys_DefaultSavePath()
{
    return ps2::system::runtime::PackageRoot();
}

// ------------------------------------------------------------------------------------------------
// Language, time and unavailable OS services
// ------------------------------------------------------------------------------------------------

namespace
{

static char s_seconds[64];
static char s_timestamp[64];

[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("platform capability unavailable: %s", method);
}

} // namespace

idCVar sys_lang("sys_lang", ID_LANG_ENGLISH, CVAR_SYSTEM | CVAR_INIT, "language identifier for resource lookup");

// The UI is cooked for English only (fonts, glyph sets and strings), so it is the one resource
// language: id's per-language manifest and copy loops skip English and add no variants.
int Sys_NumLangs()
{
    return 1;
}

const char * Sys_Lang(int index)
{
    return index == 0 ? ID_LANG_ENGLISH : "";
}

const char * Sys_DefaultLanguage()
{
    return ID_LANG_ENGLISH;
}

const char * Sys_SecToStr(int seconds)
{
    if (seconds < 0) [[unlikely]]
    {
        ps2::FatalError("Sys_SecToStr requires a nonnegative duration");
    }

    const int weeks     = seconds / 604800;
    const int days      = seconds / 86400 % 7;
    const int hours     = seconds / 3600 % 24;
    const int minutes   = seconds / 60 % 60;
    const int remaining = seconds % 60;

    if (weeks != 0)
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%dw, %dd, %d:%02d:%02d", weeks, days, hours, minutes, remaining);
    }
    else if (days != 0)
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%dd, %d:%02d:%02d", days, hours, minutes, remaining);
    }
    else
    {
        idStr::snPrintf(s_seconds, static_cast<int>(sizeof(s_seconds)), "%d:%02d:%02d", hours, minutes, remaining);
    }

    return s_seconds;
}

const char * Sys_TimeStampToStr(ID_TIME_T timestamp)
{
    // Format supplied values in UTC; this does not validate storage-driver timestamps or a wall clock.
    const time_t value = static_cast<time_t>(timestamp);
    if (timestamp < 0 || static_cast<ID_TIME_T>(value) != timestamp)
    {
        return "timestamp unavailable";
    }

    const tm * calendar = gmtime(&value);
    if (calendar == nullptr || strftime(s_timestamp, sizeof(s_timestamp), "%Y-%m-%d %H:%M UTC", calendar) == 0)
    {
        return "timestamp unavailable";
    }

    return s_timestamp;
}

PS2_COLD_FUNC void Sys_Quit()
{
#if PS2_D3BFG_TESTS
    Unsupported("Sys_Quit");
#else // PS2_D3BFG_TESTS
    ps2::system::runtime::RequestQuit();
#endif // PS2_D3BFG_TESTS
}

void Sys_Launch(const char *, idCmdArgs &, void *, unsigned int)
{
    Unsupported("Sys_Launch");
}

char * Sys_GetClipboardData()
{
    Unsupported("Sys_GetClipboardData");
}

void Sys_GrabMouseCursor(bool)
{
#if PS2_D3BFG_TESTS
    Unsupported("Sys_GrabMouseCursor");
#else // PS2_D3BFG_TESTS
    // The native console toggles capture; this console has no desktop cursor or mouse device.
#endif // PS2_D3BFG_TESTS
}

void Sys_SetRumble(int, int, int)
{
    Unsupported("Sys_SetRumble");
}

void Sys_SetPhysicalWorkMemory(int, int)
{
    Unsupported("Sys_SetPhysicalWorkMemory");
}

PS2_COLD_FUNC int Sys_GetDriveFreeSpace(const char *)
{
    Unsupported("Sys_GetDriveFreeSpace");
}

PS2_COLD_FUNC int64 Sys_GetDriveFreeSpaceInBytes(const char *)
{
    Unsupported("Sys_GetDriveFreeSpaceInBytes");
}
