// ================================================================================================
// File: iop_boot.cpp
// Brief: The IOP boot and package search (host:, then HDD, then USB), and the IOP drivers started
//        after it (pad, keyboard). Adapted from quake2-ps2's iop_boot, which always resets the IOP
//        off host: as well, without its option to keep a launcher's modules.
//
//  Off host:, both drive paths run on iomanX and fileXio: HDD on DEV9, ATAD, APA and PFS, USB on
//  BDM, FatFs, usbd and usbmass_bd. fileXioInit() routes the C library's files, and with them the
//  engine's whole filesystem, through iomanX, which serves the mounted pfs0: partition or the
//  massN: volumes. iomanX also hooks the ROM's ioman, so the ROM's LOADFILE starts the loose
//  modules in irx/ from those drives too.
//
//  The reset leaves only the boot ROM's modules, whatever the launcher ran: its USB stack, a
//  memory card or pad driver, or nothing at all (LoadExecPS2 and ps2sdk's elf-loader reset the IOP
//  themselves). That makes the drive unreachable until the game's own storage drivers run, so
//  those come from EE memory: the boot data (iop_modules.S), which the heap takes back once they
//  have started.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/iop_boot.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <hdd-ioctl.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <libpad.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <timer.h>

// The header refuses direct fileXio use beside the C library unless told the caller knows what it
// is doing: file reads go through the backend fileXioInit() installs, and the direct calls below
// only mount, list and configure the drives.
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

// The embedded modules (iop_modules.S), each <name>_irx to <name>_irx_end. Global scope: extern "C"
// in an anonymous namespace would give them internal linkage.
#define PS2_DECLARE_IOP_MODULE(name)       \
    extern "C" const u8 name##_irx[];      \
    extern "C" const u8 name##_irx_end[]

PS2_DECLARE_IOP_MODULE(iomanX);
PS2_DECLARE_IOP_MODULE(fileXio);
PS2_DECLARE_IOP_MODULE(ps2dev9);
PS2_DECLARE_IOP_MODULE(ps2atad);
PS2_DECLARE_IOP_MODULE(ps2hdd);
PS2_DECLARE_IOP_MODULE(ps2fs);
PS2_DECLARE_IOP_MODULE(bdm);
PS2_DECLARE_IOP_MODULE(bdmfs_fatfs);
PS2_DECLARE_IOP_MODULE(usbd);
PS2_DECLARE_IOP_MODULE(usbmass_bd);

#undef PS2_DECLARE_IOP_MODULE

namespace ps2::system::iop
{
namespace
{

// ------------------------------------------------------------------------------------------------
// The modules
// ------------------------------------------------------------------------------------------------

// APA holds an open handle for each PFS mount, plus one while listing partitions. PFS defaults to
// two open files, too few for the resource containers, a map's files and the probes together. Its
// cache needs at least 2 x open files + 8 buffers.
static constexpr char kHddArgs[] = "-o\0" "4\0" "-n\0" "20";
static constexpr char kPfsArgs[] = "-m\0" "1\0" "-o\0" "16\0" "-n\0" "40";

struct EmbeddedModule
{
    const char * name;
    const u8 * image;
    const u8 * end;
    const char * args;
    u32 argsBytes;
};

static constexpr EmbeddedModule kIomanX     = { "iomanX", iomanX_irx, iomanX_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kFileXio    = { "fileXio", fileXio_irx, fileXio_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kPs2Dev9    = { "ps2dev9", ps2dev9_irx, ps2dev9_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kPs2Atad    = { "ps2atad", ps2atad_irx, ps2atad_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kPs2Hdd     = { "ps2hdd", ps2hdd_irx, ps2hdd_irx_end, kHddArgs, sizeof(kHddArgs) };
static constexpr EmbeddedModule kPs2Fs      = { "ps2fs", ps2fs_irx, ps2fs_irx_end, kPfsArgs, sizeof(kPfsArgs) };
static constexpr EmbeddedModule kBdm        = { "bdm", bdm_irx, bdm_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kBdmFsFatfs = { "bdmfs_fatfs", bdmfs_fatfs_irx, bdmfs_fatfs_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kUsbd       = { "usbd", usbd_irx, usbd_irx_end, nullptr, 0 };
static constexpr EmbeddedModule kUsbmassBd  = { "usbmass_bd", usbmass_bd_irx, usbmass_bd_irx_end, nullptr, 0 };

// Each drive's chain, in load order: each module imports the one before it.
static constexpr const EmbeddedModule * kHddModules[] = { &kPs2Dev9, &kPs2Atad, &kPs2Hdd, &kPs2Fs };
static constexpr const EmbeddedModule * kUsbModules[] = { &kBdm, &kBdmFsFatfs, &kUsbd, &kUsbmassBd };

// fileXio serves a read in pieces the size of its IOP buffer, each a device read and a DMA to the
// EE waited out before the next. Its 16 KB default makes every PFS read reach the drive as 16 KB
// transfers; 64 KB is the most usbmass_bd asks of a drive in one SCSI command. FatFs also ends each
// read at a cluster's end, so on USB the larger buffer pays only with clusters over 16 KB.
static constexpr int kStockFileXioBufferBytes = 16 * 1024;
static constexpr int kFileXioBufferBytes = 64 * 1024;

// ------------------------------------------------------------------------------------------------
// The package
// ------------------------------------------------------------------------------------------------

// The file that makes a folder the package: id's filesystem refuses to start without it.
static constexpr const char * kPackageProbe = "base/default.cfg";

static constexpr const char * kHddDevices[] = { "hdd0:", "hdd1:" };
static constexpr const char * kHddMountPath = "pfs0:";
static constexpr int kMountReadWrite = 0; // FIO_MT_RDWR in io_common.h, which C++ must not include.
static constexpr int kMaxPartitionLen = static_cast<int>(sizeof("hdd0:") - 1) + APA_IDMAX;

// bdmfs_fatfs serves each FAT/exFAT volume it mounts as mass0: to mass9:, in mount order. A drive
// partition is a volume, and usbmass_bd drives two drives at once, so the package need not be on
// mass0:. A bare "mass:" is mass0:, since iomanX reads a missing unit number as 0.
static constexpr const char * kUsbUnits[] = {
    "mass0:", "mass1:", "mass2:", "mass3:", "mass4:", "mass5:", "mass6:", "mass7:", "mass8:", "mass9:",
};

// The engine builds "<root>/base/<name>" within its 256-byte paths; a folder of up to 64 characters
// leaves 180 for the name.
static constexpr int kMaxElfFolderLen = 64;
static constexpr int kMaxRootLen = static_cast<int>(sizeof("mass0:") - 1) + kMaxElfFolderLen;

// The drive enumerates and its FAT mounts after usbmass_bd starts, and a drive that spins up takes
// seconds. The search returns as soon as the package shows up, so only a failing boot waits it out.
static constexpr int kUsbWaitMsec = 10000;
static constexpr int kUsbWaitStepMsec = 100;

static char s_root[kMaxRootLen + 1] = {};
static char s_elfFolder[kMaxElfFolderLen + 1] = {}; // "/dir/sub", or empty for the drive's root
static char s_candidate[kMaxRootLen + 1] = {};
static const char * s_hddStatus = "not searched";
static int s_hddMounts = 0;
static int s_hddMountMsec = 0;
static bool s_booted = false;
static bool s_reset = false;
static bool s_usbdRunning = false;

// A partition listing's entry, written by SIF DMA: it owns whole cache lines, so no neighbour's
// dirty line can be written back over the transfer.
union alignas(64) DirentBuffer
{
    iox_dirent_t entry;
    u8 lines[(sizeof(iox_dirent_t) + 63) & ~size_t{ 63 }];
};

static DirentBuffer s_dirent;

// ------------------------------------------------------------------------------------------------
// The report (see PrintBootReport)
// ------------------------------------------------------------------------------------------------

static constexpr size_t kReportBytes = 3072;
static constexpr size_t kReportLineBytes = 384;
static char s_report[kReportBytes] = {};
static size_t s_reportUsed = 0;

// Logs a line and keeps it for the report.
PS2_COLD_FUNC PS2_PRINTF_FUNC(1, 2) void Report(const char * format, ...)
{
    // The launch line carries the loader's path, a whole host path in the emulator.
    char line[kReportLineBytes];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    Log(LogLevel::Info, "[D3BFG] IOP %s\n", line);

    const size_t room = kReportBytes - s_reportUsed;

    if (room > 1)
    {
        const int written = std::snprintf(&s_report[s_reportUsed], room, "%s\n", line);

        if (written > 0)
        {
            const auto length = static_cast<size_t>(written);
            s_reportUsed += length < room ? length : room - 1;
        }
    }
}

// Milliseconds since the EE timer started, for the report's times.
PS2_COLD_FUNC int NowMsec()
{
    u32 seconds = 0;
    u32 microseconds = 0;
    TimerBusClock2USec(GetTimerSystemTime(), &seconds, &microseconds);
    return static_cast<int>(seconds * 1000u + microseconds / 1000u);
}

// Crude, but boot-time polling needs no more.
PS2_COLD_FUNC void WaitMsec(int msec)
{
    const int until = NowMsec() + msec;

    while (NowMsec() < until)
    {
    }
}

// The largest block of IOP memory that can be allocated now, to 4 KB: the EE has no call that
// says how much is free, so this halves its way between sizes that fit and ones that don't.
PS2_COLD_FUNC int LargestFreeIopBlock()
{
    if (SifInitIopHeap() < 0)
    {
        return 0;
    }

    static constexpr int kIopRamBytes = 2 * 1024 * 1024;
    static constexpr int kPrecisionBytes = 4 * 1024;
    int fits = 0;
    int fails = kIopRamBytes;

    while (fails - fits > kPrecisionBytes)
    {
        const int size = ((fits + fails) / 2) & ~255;
        void * block = SifAllocIopHeap(size);

        if (block != nullptr)
        {
            SifFreeIopHeap(block);
            fits = size;
        }
        else
        {
            fails = size;
        }
    }

    return fits;
}

// ------------------------------------------------------------------------------------------------
// Finding the package
// ------------------------------------------------------------------------------------------------

PS2_COLD_FUNC bool HasPackage(const char * root)
{
    char path[kMaxRootLen + 32];
    std::snprintf(path, sizeof(path), "%s/%s", root, kPackageProbe);
    std::FILE * file = std::fopen(path, "rb");

    if (file == nullptr)
    {
        return false;
    }

    std::fclose(file);
    return true;
}

PS2_COLD_FUNC bool IsSeparator(char letter)
{
    return letter == '/' || letter == '\\';
}

// The launch partition, kept before the reset drops the loader's PFS mount. The ELF loader's form
// (hdd0:+DOOM3:pfs:/dir/d3bfg.elf) and the file browser's (hdd0:/+DOOM3/dir/d3bfg.elf) both name
// it; a bare pfsN: path cannot, and the search then lists the partitions instead.
PS2_COLD_FUNC bool PartitionFromPath(const char * elfPath, char (&partition)[kMaxPartitionLen + 1])
{
    partition[0] = '\0';

    if (elfPath == nullptr || (std::strncmp(elfPath, "hdd0:", 5) != 0 && std::strncmp(elfPath, "hdd1:", 5) != 0))
    {
        return false;
    }

    const char * start = elfPath + 5;

    while (IsSeparator(*start))
    {
        ++start;
    }

    const char * end = start;

    while (*end != '\0' && *end != ':' && !IsSeparator(*end))
    {
        ++end;
    }

    const auto length = static_cast<int>(end - start);

    if (length == 0 || length > APA_IDMAX || *end == '\0')
    {
        return false;
    }

    if (*end == ':')
    {
        const char * device = end + 1;
        const bool pfs = std::strncmp(device, "pfs:", 4) == 0 ||
                         (std::strncmp(device, "pfs", 3) == 0 && device[3] >= '0' && device[3] <= '9' && device[4] == ':');

        if (!pfs)
        {
            return false;
        }
    }

    std::memcpy(partition, elfPath, 5);
    std::memcpy(partition + 5, start, static_cast<size_t>(length));
    partition[5 + length] = '\0';
    return true;
}

// The folder the loader launched the ELF from, so the package can sit anywhere on the drive.
// Loaders spell the device their own way ("mass:/APPS/D3/d3bfg.elf", "mass0:APPS/D3/d3bfg.elf",
// "hdd0:+DOOM3:pfs:/...") and the reset renumbers drives anyway, so only what follows the last
// colon is kept, and every unit is tried with it. Writes "/APPS/D3", whatever the separators, or
// an empty string for the root or a null path. False, with an empty string, for a folder too long
// for the engine's paths.
PS2_COLD_FUNC bool ElfFolderFromPath(const char * elfPath, char (&folder)[kMaxElfFolderLen + 1])
{
    folder[0] = '\0';

    if (elfPath == nullptr)
    {
        return true;
    }

    const char * path = elfPath;

    for (const char * cursor = elfPath; *cursor != '\0'; ++cursor)
    {
        if (*cursor == ':')
        {
            path = cursor + 1;
        }
    }

    // The browser form puts the APA partition in the first component, which is not part of the
    // folder inside PFS. The last-colon rule above handled the canonical HDD form.
    if ((std::strncmp(elfPath, "hdd0:", 5) == 0 || std::strncmp(elfPath, "hdd1:", 5) == 0) &&
        std::strchr(elfPath + 5, ':') == nullptr)
    {
        path = elfPath + 5;

        while (IsSeparator(*path))
        {
            ++path;
        }

        while (*path != '\0' && !IsSeparator(*path))
        {
            ++path;
        }
    }

    // The folder ends at the last separator; without one the ELF sits at the root.
    const char * folderEnd = path;

    for (const char * cursor = path; *cursor != '\0'; ++cursor)
    {
        if (IsSeparator(*cursor))
        {
            folderEnd = cursor;
        }
    }

    // A name at a time, which drops leading, doubled and trailing separators.
    int length = 0;

    for (const char * cursor = path; cursor < folderEnd;)
    {
        if (IsSeparator(*cursor))
        {
            ++cursor;
            continue;
        }

        const char * nameEnd = cursor;

        while (nameEnd < folderEnd && !IsSeparator(*nameEnd))
        {
            ++nameEnd;
        }

        const auto nameLength = static_cast<int>(nameEnd - cursor);

        if (length + 1 + nameLength > kMaxElfFolderLen)
        {
            folder[0] = '\0';
            return false;
        }

        folder[length++] = '/';
        std::memcpy(&folder[length], cursor, static_cast<size_t>(nameLength));
        length += nameLength;
        cursor = nameEnd;
    }

    folder[length] = '\0';
    return true;
}

// "mass:" or "mass0:" to "mass9:". Such a boot looks on USB only: searching the HDD first cost
// quake2-ps2 24.6 s of PFS mounts on a console with a hard disk attached before USB was looked at.
PS2_COLD_FUNC bool LaunchedFromUsb(const char * elfPath)
{
    if (elfPath == nullptr || std::strncmp(elfPath, "mass", 4) != 0)
    {
        return false;
    }

    const char next = elfPath[4];
    return next == ':' || (next >= '0' && next <= '9' && elfPath[5] == ':');
}

// A folder ("" for the root) on every USB volume; the package's root, or null.
PS2_COLD_FUNC const char * SearchUsbVolumes(const char * folder)
{
    for (const char * unit : kUsbUnits)
    {
        std::snprintf(s_candidate, sizeof(s_candidate), "%s%s", unit, folder);

        if (HasPackage(s_candidate))
        {
            return s_candidate;
        }
    }

    return nullptr;
}

// A FAT volume is mounted behind a massN: unit only when its root opens.
PS2_COLD_FUNC bool UsbVolumeMounted(const char * unit)
{
    char root[16];
    std::snprintf(root, sizeof(root), "%s/", unit);
    DIR * directory = opendir(root);

    if (directory == nullptr)
    {
        return false;
    }

    closedir(directory);
    return true;
}

// ------------------------------------------------------------------------------------------------
// Starting modules
// ------------------------------------------------------------------------------------------------

// The boot ROM's LOADFILE 1.x cannot load a module out of EE memory, and on some models refuses a
// module path off rom0:; the sbv patches lift both. sbv answers 1 without patching for a LOADFILE
// that loads from EE memory already (2.x) and for one it doesn't recognise; with the latter the
// modules would not start, and fileXioInit fails.
PS2_COLD_FUNC bool PatchModuleLoader(bool & patched)
{
    const int lmb = sbv_patch_enable_lmb();
    patched = lmb == 0;
    return lmb >= 0 && sbv_patch_disable_prefix_check() == 0;
}

// A fresh IOP: only the boot ROM's modules run, whatever the launcher left. ps2sdk counts the
// reset, so the EE libraries bound before it (fio, loadfile) bind again on their next call.
PS2_COLD_FUNC void ResetIop()
{
    const int startMsec = NowMsec();
    SifInitRpc(0);

    while (!SifIopReset("", 0))
    {
    }

    while (!SifIopSync())
    {
    }

    SifInitRpc(0);
    bool patched = false;

    if (!PatchModuleLoader(patched)) [[unlikely]]
    {
        FatalError("IOP boot: the module loader patches failed");
    }

    s_reset = true;
    Report("reset the IOP in %d ms; module loader %s", NowMsec() - startMsec,
           patched ? "patched to load from EE memory" : "left as it was (LOADFILE 2.x, or one sbv doesn't know)");
}

// Starts a module out of the boot data. False when it isn't running afterwards: a negative id is a
// load that failed, result 1 (NO_RESIDENT_END) a module whose start bailed out, and some drivers
// return a negative init error.
PS2_COLD_FUNC bool StartEmbedded(const EmbeddedModule & module)
{
    const int startMsec = NowMsec();
    const auto bytes = static_cast<u32>(module.end - module.image);
    int result = 0;
    const int id = SifExecModuleBuffer(const_cast<u8 *>(module.image), bytes, module.argsBytes, module.args, &result);

    if (id < 0 || result < 0 || result == 1)
    {
        Report("module %s didn't start (id %d, result %d)", module.name, id, result);
        return false;
    }

    Report("started %s (%u bytes, id %d) in %d ms", module.name, bytes, id, NowMsec() - startMsec);
    return true;
}

// A module the boot cannot go on without: after the reset there is nothing to fall back on.
PS2_COLD_FUNC void StartRequired(const EmbeddedModule & module)
{
    if (!StartEmbedded(module)) [[unlikely]]
    {
        FatalError("IOP boot: module %s didn't start", module.name);
    }
}

// Starts a module from the package's irx/ directory, with the same checks as StartEmbedded.
PS2_COLD_FUNC bool StartLoose(const char * name)
{
    char path[kMaxRootLen + 32];
    std::snprintf(path, sizeof(path), "%s/irx/%s.irx", s_root, name);
    int result = 0;
    const int id = SifLoadStartModule(path, 0, nullptr, &result);

    if (id < 0 || result < 0 || result == 1)
    {
        Log(LogLevel::Warning, "[D3BFG] IOP module %s didn't start (id %d, result %d)", path, id, result);
        return false;
    }

    Log(LogLevel::Info, "[D3BFG] IOP started %s (id %d)\n", path, id);
    return true;
}

PS2_COLD_FUNC bool StartRom(const char * path)
{
    int result = 0;
    const int id = SifLoadStartModule(path, 0, nullptr, &result);

    if (id < 0 || result == 1)
    {
        Log(LogLevel::Warning, "[D3BFG] IOP module %s didn't start (id %d, result %d)", path, id, result);
        return false;
    }

    return true;
}

// ------------------------------------------------------------------------------------------------
// The drives
// ------------------------------------------------------------------------------------------------

PS2_COLD_FUNC const char * ProbePartition(const char * partition, const char * folder)
{
    const int startMsec = NowMsec();

    if (fileXioMount(kHddMountPath, partition, kMountReadWrite) < 0)
    {
        s_hddMountMsec += NowMsec() - startMsec;
        return nullptr;
    }

    ++s_hddMounts;
    std::snprintf(s_candidate, sizeof(s_candidate), "%s%s", kHddMountPath, folder);
    const bool found = HasPackage(s_candidate);

    // The package's mount stays: every later file goes through pfs0:.
    if (!found)
    {
        fileXioUmount(kHddMountPath);
    }

    s_hddMountMsec += NowMsec() - startMsec;

    if (!found)
    {
        return nullptr;
    }

    Report("package on %s (HDD partition %s)", s_candidate, partition);
    return s_candidate;
}

// The launch partition's folder, then its root, then every other main PFS partition's folder, and
// then their roots. APA subpartitions belong to their main partition.
PS2_COLD_FUNC const char * FindOnHdd(const char * launchPartition)
{
    // No DEV9 or hard disk is normal (a slim console, a USB-only setup): a driver that won't start
    // leaves USB to search. Nothing here formats anything.
    s_hddStatus = "drivers unavailable";

    for (const EmbeddedModule * module : kHddModules)
    {
        if (!StartEmbedded(*module))
        {
            return nullptr;
        }
    }

    s_hddStatus = "no package on a PFS partition";

    if (launchPartition[0] != '\0')
    {
        if (const char * root = ProbePartition(launchPartition, s_elfFolder))
        {
            return root;
        }

        if (s_elfFolder[0] != '\0')
        {
            if (const char * root = ProbePartition(launchPartition, ""))
            {
                return root;
            }
        }
    }

    const char * const folders[] = { s_elfFolder, "" };
    const int folderCount = s_elfFolder[0] != '\0' ? 2 : 1;

    for (int folderIndex = 0; folderIndex < folderCount; ++folderIndex)
    {
        for (const char * device : kHddDevices)
        {
            const int status = fileXioDevctl(device, HDIOC_STATUS, nullptr, 0, nullptr, 0);

            if (status != 0)
            {
                if (folderIndex == 0)
                {
                    Report("%s unavailable (status %d)", device, status);
                }

                continue;
            }

            const int directory = fileXioDopen(device);

            if (directory < 0)
            {
                continue;
            }

            while (fileXioDread(directory, &s_dirent.entry) > 0)
            {
                const iox_dirent_t & entry = s_dirent.entry;

                if (entry.stat.mode != APA_TYPE_PFS || (entry.stat.attr & APA_FLAG_SUB) != 0)
                {
                    continue;
                }

                char partition[kMaxPartitionLen + 1];
                std::snprintf(partition, sizeof(partition), "%s%.*s", device, APA_IDMAX, entry.name);

                if (std::strcmp(partition, launchPartition) == 0)
                {
                    continue;
                }

                if (const char * root = ProbePartition(partition, folders[folderIndex]))
                {
                    fileXioDclose(directory);
                    return root;
                }
            }

            fileXioDclose(directory);
        }
    }

    return nullptr;
}

PS2_COLD_FUNC const char * FindOnUsb()
{
    for (const EmbeddedModule * module : kUsbModules)
    {
        StartRequired(*module);
    }

    s_usbdRunning = true;

    for (int waited = 0; waited <= kUsbWaitMsec; waited += kUsbWaitStepMsec)
    {
        // Next to the ELF first: that is the copy the player launched, whatever the root holds.
        const char * root = s_elfFolder[0] != '\0' ? SearchUsbVolumes(s_elfFolder) : nullptr;

        if (root == nullptr)
        {
            root = SearchUsbVolumes("");
        }

        if (root != nullptr)
        {
            Report("package on %s after %d ms of waiting for the drive", root, waited);
            return root;
        }

        WaitMsec(kUsbWaitStepMsec);
    }

    Report("USB: no package after %d ms", kUsbWaitMsec);
    return nullptr;
}

// fileXio's larger read buffer, once the drives' own modules have taken their IOP memory.
PS2_COLD_FUNC void SetFileXioBuffer()
{
    const int result = fileXioSetRWBufferSize(kFileXioBufferBytes);

    if (result == 0)
    {
        Report("fileXio read buffer %d KB", kFileXioBufferBytes / 1024);
        return;
    }

    // fileXio frees its buffer before allocating the new one and keeps a null one when that
    // fails, which every read would then use: put the stock size back.
    Report("no IOP memory for a %d KB fileXio read buffer (%d); keeping %d KB", kFileXioBufferBytes / 1024, result,
           kStockFileXioBufferBytes / 1024);

    if (fileXioSetRWBufferSize(kStockFileXioBufferBytes) != 0) [[unlikely]]
    {
        FatalError("IOP boot: fileXio has no read buffer left");
    }
}

PS2_COLD_FUNC void TakeRoot(const char * root)
{
    std::snprintf(s_root, sizeof(s_root), "%s", root);
    const size_t released = heap::ReleaseBootData();
    Report("package root %s at %d ms; %u bytes of boot data returned to the heap", s_root, NowMsec(),
           static_cast<u32>(released));
}

// ------------------------------------------------------------------------------------------------
// The drivers started after the boot
// ------------------------------------------------------------------------------------------------

enum class DriverStart : u8
{
    NotTried,
    Up,
    Failed
};

static DriverStart s_padDriver = DriverStart::NotTried;
static DriverStart s_keyboardDriver = DriverStart::NotTried;

PS2_COLD_FUNC bool StartOnce(DriverStart & state, bool (*bringUp)())
{
    if (state == DriverStart::NotTried)
    {
        state = bringUp() ? DriverStart::Up : DriverStart::Failed;
    }

    return state == DriverStart::Up;
}

// The ROM's PADMAN runs on the ROM's SIO2MAN; every model's ROM has both.
PS2_COLD_FUNC bool BringUpPad()
{
    if (!StartRom("rom0:SIO2MAN") || !StartRom("rom0:PADMAN"))
    {
        return false;
    }

    if (padInit(0) != 1)
    {
        Log(LogLevel::Warning, "[D3BFG] IOP pad library did not start");
        return false;
    }

    return true;
}

// ps2kbd speaks USB, so usbd goes first; a USB boot started it already.
PS2_COLD_FUNC bool BringUpKeyboard()
{
    if (!s_usbdRunning && !StartLoose("usbd"))
    {
        return false;
    }

    s_usbdRunning = true;
    return StartLoose("ps2kbd");
}

} // namespace

PS2_COLD_FUNC const char * Boot(const char * elfPath)
{
    if (s_booted) [[unlikely]]
    {
        FatalError("IOP boot ran twice");
    }

    s_booted = true;

    // The emulator's host filesystem serves the ELF's folder with nothing on the IOP, and a
    // console without one fails the probe at once.
    if (HasPackage("host:"))
    {
        TakeRoot("host:");
        return s_root;
    }

    char launchPartition[kMaxPartitionLen + 1];
    PartitionFromPath(elfPath, launchPartition);
    const bool fromUsb = LaunchedFromUsb(elfPath);

    // The folder and partition, read before the reset drops the loader's mounts.
    char searched[kMaxElfFolderLen + 64];

    if (!ElfFolderFromPath(elfPath, s_elfFolder))
    {
        std::snprintf(searched, sizeof(searched), "the drive's root only (ELF folder over %d characters)", kMaxElfFolderLen);
    }
    else if (s_elfFolder[0] != '\0')
    {
        std::snprintf(searched, sizeof(searched), "%s, then the drive's root", s_elfFolder);
    }
    else
    {
        std::snprintf(searched, sizeof(searched), "the drive's root");
    }

    const char * launchedAs = elfPath != nullptr && elfPath[0] != '\0' ? elfPath : "(no path from the loader)";
    Report("launched as '%s'; looking %s for %s in %s", launchedAs, fromUsb ? "on USB" : "on the HDD, then USB",
           kPackageProbe, searched);

    ResetIop();
    StartRequired(kIomanX);
    StartRequired(kFileXio);

    // Both PFS and FatFs register with iomanX, which the C library reaches through fileXio rather
    // than the ROM's FILEIO from here on.
    if (fileXioInit() < 0) [[unlikely]]
    {
        FatalError("IOP boot: fileXio didn't initialize");
    }

    const char * root = nullptr;

    if (fromUsb)
    {
        s_hddStatus = "not searched (launched from USB)";
    }
    else
    {
        const int hddStartMsec = NowMsec();
        root = FindOnHdd(launchPartition);
        Report("HDD: %s after %d ms; %d PFS mounts took %d ms of it", root != nullptr ? "package found" : s_hddStatus,
               NowMsec() - hddStartMsec, s_hddMounts, s_hddMountMsec);
    }

    if (root == nullptr)
    {
        root = FindOnUsb();
    }

    if (root != nullptr)
    {
        SetFileXioBuffer();
        TakeRoot(root);
        Report("%d KB of IOP memory free in one block", LargestFreeIopBlock() / 1024);
        return s_root;
    }

    // A console has no log to read: say how far the drive got. No volume means no FAT/exFAT drive
    // came up in time; a volume means the package is not where it was looked for.
    char volumes[ArrayLength(kUsbUnits) * sizeof(" massN:")] = {};

    for (const char * unit : kUsbUnits)
    {
        if (UsbVolumeMounted(unit))
        {
            std::strcat(volumes, " ");
            std::strcat(volumes, unit);
        }
    }

    FatalError("No game package found!\n"
               "Put d3bfg.elf, base/ and irx/ in one folder, on an APA/PFS\n"
               "partition or a FAT32/exFAT USB drive.\n"
               "Launched as: %s\n"
               "Looked in: %s\n"
               "HDD: %s\n"
               "USB volumes mounted:%s",
               launchedAs, searched, s_hddStatus, volumes[0] != '\0' ? volumes : " none");
}

PS2_COLD_FUNC void PrintBootReport(void (*printLine)(const char * line))
{
    if (!s_reset)
    {
        return;
    }

    for (const char * line = s_report; *line != '\0';)
    {
        const char * end = std::strchr(line, '\n');
        const size_t length = end != nullptr ? static_cast<size_t>(end - line) : std::strlen(line);
        char text[kReportLineBytes + 16];
        std::snprintf(text, sizeof(text), "IOP boot: %.*s", static_cast<int>(length), line);
        printLine(text);
        line += length + (end != nullptr ? 1 : 0);
    }
}

PS2_COLD_FUNC bool StartPadDriver()
{
    return StartOnce(s_padDriver, BringUpPad);
}

PS2_COLD_FUNC bool StartKeyboardDriver()
{
    PS2_AssertMsg(s_booted, "the keyboard driver starts after the IOP boot");
    return StartOnce(s_keyboardDriver, BringUpKeyboard);
}

} // namespace ps2::system::iop
