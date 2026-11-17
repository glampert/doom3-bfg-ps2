# PS2 platform: Doom foundation and Quake II reference

## Verified Doom foundation

Production boots in PCSX2 with `src/ps2/system/main.cpp`; the core test driver is
`src/tests/smoketests/core_tests_main.cpp`. Both initialize SIF RPC and use SDK bus-clock reads;
the core driver and production on `host:` preserve the loader's IOP/HostFs. Target fixtures verify
timer conversion/progress, 64-byte buffers and finite EE divide-by-zero behavior.
The heap takes all of sbrk's range in crt0's weak `_ps2sdk_memory_init` hook, which runs after
`SetupHeap` and before libcglue's startup and every constructor; nothing else may call sbrk.
The kernel puts the main stack at the top of RAM and `EndOfHeap()` is its bottom, so the arena's
top (the heap's first small pages) sits right under it. The SDK linkfile only `PROVIDE`s
`_stack_size` (128 KiB); `heap.cpp` defines it (256 KiB, `heap::kMainStackBytes`) with `.set`.
The retail menu session peaked at 154,376 bytes, past the old 128 KiB: dlmalloc grew up from
`_end`, so the overflow landed in its untouched top unnoticed, while the new heap's cvars there
lost their vtables (2026-11-17). Tracking builds paint the stack in `EnterMain`; every Common
frame's `heap::CheckStack` stops once its bottom 1 KiB is touched, and `STACK` lines report the
untouched headroom.
There is no custom EE exception handler yet. Recorded results and limits are in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

**IOP boot** (`system/iop_boot.cpp`, from quake2-ps2, 2026-11-17): production finds its package
(`base/default.cfg`) on `host:` with the IOP untouched, or resets the IOP and searches the HDD
(PFS) and then USB (BDM) itself; the result is `runtime::PackageRoot()`. After a reset no drive is
readable until the game's storage drivers run, so those ten IRX are `.incbin`'d into `.bootdata`
(`iop_modules.S`), linked at the first page past `_end` (`config/linkfile.ld`, every image), which
the heap holds as its first block until `heap::ReleaseBootData()`. Later drivers load from the
package's `irx/` through the ROM LOADFILE, which reaches `mass0:`/`pfs0:` through iomanX's ioman
hook once `sbv_patch_disable_prefix_check` has run. Through fileXio, **`closedir` returns the
IOP's close result** (2 on bdmfs_fatfs, PCSX2): only -1 is a failure. The USB path has run in
PCSX2's emulated USB storage ([production.md](../../docs/testing/production.md#console-storage-boot));
HDD and hardware have not.

The filesystem slice verifies streaming reads (including an authored 70 KiB file),
directory enumeration/stat, mkdir/rmdir and write/append on the preserved HostFs.
`sbv_patch_fileio()` runs once after SIF initialization, without an IOP reset. Its
FILEIO 1.01 fix covers remove fallthrough and getstat/dread interrupt protection;
removal fails with `ENOSYS` before/without that patch. ps2sdk's default fio path-ops
table has no rename callback, so libc rename returns `ENOSYS`. Keep that failure
instead of substituting non-atomic copy/delete.

PCSX2 2.6.3 removal of an authored file returned `-1`/`ENODEV` even though the file
was removed and no same-named directory remained. Keep the real result observable;
this is not successful removal acceptance on hardware. Inference: its
[remove HLE](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2/IopBios.cpp#L767)
returns 0 after handling a host path, unlike mkdir/rmdir, consistent with a later IOP
fallback overriding the result. The regression requires this exact error plus absence
of both file and directory. Do not generalize this behavior to other drivers.

Portable paths are bounded to 256 bytes, retain forward slashes and recognize device
prefixes. Logical paths reject absolute/device names and dot components; they do not
perform case-folded OS lookups or resolve symlink targets. Directory lists exclude
symlinks. SDK `io_to_posix_time` expects a two-byte full year and does not initialize
all `tm` fields; PCSX2's [host stat conversion](https://github.com/PCSX2/pcsx2/blob/v2.6.3/pcsx2/IopBios.cpp#L183)
writes `tm_year` into one byte. Do not infer wall-clock accuracy from successful stat
or the current `Sys_FileTimeStamp` bridge. Validate the storage/time policy before
using timestamps for cache invalidation or save metadata.

GCC can eliminate a direct `calloc`/`free` test pair, including an `errno` observation.
The overflow regression calls through a volatile function pointer; check the generated
EE assembly when testing allocation side effects.

The shared OS-service bridge formats nonnegative durations and supplied epoch values
in UTC. It exposes English as the only language identifier (`Sys_NumLangs` 1), since the
UI is cooked for English only. Process launch, clipboard, rumble,
desktop physical-working-set control and drive-capacity queries are explicitly
unavailable. Production mouse-capture requests are harmless with no mouse device;
test variants retain their unavailable-capability failure. Never return fabricated free space or treat desktop memory
controls as EE allocation budgets.

Production `Quit` / `Sys_Quit` now request exit at the main loop's frame boundary;
Common shuts down after its current command/offline-service frame returns. Test
variants retain the explicit unavailable-operation boundary. Native filesystem
startup mounts `host:/base`, requires `default.cfg`, and optionally executes the
port's `d3bfg.cfg`. `BuildOSPath` can return a directory without a trailing slash;
`AddGameDirectory` must remove only an actual slash. Unconditionally removing the
last character scanned `host:/bas` instead of `host:/base`, observed and corrected
in the production debug/release PCSX2 runs on 2026-10-11.

## Doom-observed HostFs missing-file error

On PCSX2 2.6.3, opening an absent `host:presentation.cfg` reports an IOP result of
`-5`, mapped to `EIO` rather than `ENOENT` by the current FILEIO/libc path. The
optional production startup-policy file must preserve old headless packages, so its
open failure defaults to headless on either `ENOENT` or `EIO` and emits a warning.
This also means the backend cannot distinguish missing policy from another `EIO`
condition on this path. Other open errors, read/close failures and invalid present
content remain fatal. Do not generalize this compatibility fallback to required
assets or other storage drivers; staging writes explicit policy for normal runs.

## Doom-observed SDK timer conversion trap

The installed `ee/kernel/src/timer.c::TimerUSec2BusClock` returns `u64`, but evaluates
`seconds * kBUSCLK` in `u32`. At 147,456,000 bus ticks/second, a seconds argument of
30 or more wraps; 90 seconds becomes about 2.62 seconds. This prematurely ended the
first GS inspection hold in PCSX2 on 2026-10-14. Use a wide calculation, such as
`TimerUSec2BusClock(1, 0) * 90U`, rather than passing a long seconds value. The final
60-second inspection holds completed in both configurations. This trap belongs to
the current SDK implementation, not a hardware timer limit.

## Doom toolchain baseline

The local `~/ps2dev` toolchain was rebuilt on 2026-10-19: ps2sdk `329e2bb`, GCC 15.2.0,
binutils 2.45.1 and newlib 4.6.0 built with `HAVE_BLKSIZE`. CI pins the ps2sdk image of
the same versions ([ci.md](ci.md)). newlib now sizes stdio buffers from `st_blksize`;
quake2-ps2 measured 16 KB for `host:` files, where streams previously got 1 KB, and a seek
outside the buffer reads the aligned block below its target. Every regular Doom gate
passes on the rebuilt toolchain in debug and release, with unchanged tagged ledgers
([memory-budget.md](memory-budget.md)); HostFs command polling, page streaming and the
filesystem smoke checks behave as before in PCSX2.

## Quake II reference

SDK and EE hardware findings below are useful, but named source files, boot flows,
diagnostics and measurements describe `quake2-ps2`. Recheck them for each Doom subsystem. Doom's
`iop_boot.cpp` keeps Quake's search order, HDD/PFS arguments and USB wait, without its option to
reuse a launcher's modules, its log-file sync or its sound and memory card drivers.

## Don't trust ps2sdk on sight

- **libc stubs.** libcglue stubs a number of libc functions to fail. `sysconf()` returns -1
  with `EINVAL` for *every* name, `_SC_PAGE_SIZE` included, and `tcgetattr`/`tcsetattr` etc.
  return -1/`ENOSYS`. The dlmalloc this port used to vendor took its page size from `sysconf`,
  got `(size_t)-1`, and the ELF black-screened until it pinned 4096. Before relying on a libc call, grep
  `~/ps2dev/src/ps2dev/build/ps2sdk/ee/libcglue/src/glue.c`. Prefer pinned constants to
  runtime libc queries.
- **Register-packing macros.** `VU_GS_PRIM`/`VU_GS_GIFTAG` (packet2_utils.h) don't
  parenthesize their parameters. `blended ? 1 : 0` as ABE parsed as `blended ? 1 : (0 << 6)`,
  which put the bit inside PRIM, where it was already set, so blending silently never
  happened. Use `GIF_SET_PRIM`/`GIF_SET_TAG` (gif_tags.h), which parenthesize and mask.
  `gs_gp.h` is not uniformly safe either. `GS_SET_DIMX` masks the 3-bit signed dither entries
  with `0x3`, which turns the matrix into a brightening bias, and libdraw's
  `draw_dither_matrix` inherits that. Pack DIMX by hand: `(u64)((u32)v & 0x7u) << (i * 4)`.
  Hoist computed arguments into named locals, read the macro body, and if a GS feature
  "isn't enabled", compile the macro standalone on the host and inspect the bits.
- `packet_init`/`packet2_create` each `calloc` a header and `memalign(64)` the buffer. Read the
  SDK source when internal behaviour matters.

## EE CPU and FPU

- **No IEEE Inf/NaN on COP1.** Divide by zero saturates to ±FLT_MAX, denormals flush to zero,
  and rounding is toward zero. `Normalize(zero)` gives `(0,0,0)` on target but NaN on a host.
  This once vetoed valid ears in polygon triangulation: 714 faces with holes on target versus
  4 on host. Guard degenerate vectors explicitly. Never rely on NaN propagation.
- The compiler folds `constexpr` float math in the *target's* round-toward-zero semantics.
  `math::kPI` folds to `0x40490FDA`, not `...DB`. A host harness won't reproduce shipped
  constant bytes exactly, so check the ELF's `.rodata` instead.
- **No double-precision FPU.** `double` is libgcc soft-float. The engine C builds with
  `-fsingle-precision-constant` and `ps2/math/math_c.h`. Backend code uses `f` suffixes, and
  `-Wdouble-promotion` enforces them. Soft-float doubles were the cause of almost all
  dropped frames.
- `__builtin_ctz`/`__builtin_clz` are **libgcc calls** (the R5900 has no count-zeros
  instruction). Use `ps2::tex::Log2` (`plzcw`) or compares in hot paths.

## SIF DMA

- Any EE buffer that is a SIF DMA target (`SifRpcGetOtherData`, `sceSifSetDma`, memory card
  bounce buffers) must be `alignas(64)` and sized in whole 64-byte units. The DMAC ignores
  low address bits, and 64 bytes is the D-cache line, so a neighbour's dirty line can be
  written back over the DMA'd bytes. A plain `static char[128]` only gets 8-byte alignment.
  Check with `mips64r5900el-ps2-elf-nm build/debug/quake2_unstripped.elf | grep <sym>`.
  `SyncDCache(buf, buf + size)` before the transfer is the matching half.

## IOP modules, ROM FILEIO, memory card

- Boot (`system/iop_boot.cpp`): the `host:` fast path doesn't reset the IOP.
  `sbv_patch_enable_lmb()` alone lets `SifExecModuleBuffer` load embedded IRX without
  disturbing open `host:` handles. Console storage shares a full IOP reset, sbv patches and
  iomanX/fileXio. Boot probes host, then HDD/PFS, then USB/BDM before `Qcommon_Init`.
- **HDD drivers and limits:** standard `ps2hdd.irx` already uses iomanX; the separately named
  `ps2hdd-iomanx.irx` is a virtual-disk variant without DEV9/ATAD. Use ps2dev9 → ps2atad →
  ps2hdd → ps2fs. Missing hardware/failed HDD init must fall through to USB. APA's default
  one open handle cannot enumerate while PFS is mounted; boot uses `-o 4 -n 20`. PFS's
  default two open files cannot cover pak/music/log/asset reads; boot uses `-m 1 -o 16 -n 40`,
  supplying NUL-separated args. Cache buffers must be at least `2 * maxOpen + 8`.
- **HDD selection:** retain the launch partition before reset; mount it read/write at `pfs0:`
  so the debug log can be written. Other partitions are enumerated from `hdd0:`/`hdd1:`:
  `HDIOC_STATUS == 0`, `stat.mode == APA_TYPE_PFS`, and no `APA_FLAG_SUB`. These constants
  are in `hdd-ioctl.h`. The `iox_dirent_t` DMA destination must own aligned 64-byte lines.
  Keep the winning mount alive and unmount misses before probing the next partition.
- **PFS close does not always persist metadata.** Standard read/write mount flag `0` leaves
  dirty inode/directory metadata cached: `pfsFioCloseFileSlot` only calls
  `pfsCacheFlushAllDirty` when mount flag `0x02` is set (`PFS_MT_ROBUST` in `libhdd.h`).
  `fileXioSync("pfs0:", 0)` explicitly flushes it. `system/iop_boot.cpp` keeps the selected
  HDD mount internal and exposes `SyncGameDataDevice()`, which returns 0 without an RPC for
  host/USB. `debug/log_file.cpp` calls it after each close, including header and fatal output,
  without knowing fileXio or the mount name. Check close/sync results and disable logging on
  failure. Sync uses the default blocking `FXIO_WAIT` mode and returns a negative SDK error
  directly rather than setting libc errno. The original missing-log diagnosis remains
  unconfirmed until a new HDD run; the available emulator log was from a later host run.
- **USB presence is not module-loader readiness.** HDD success has reset/patched the IOP
  without starting usbd. Track those separately so keyboard startup starts usbd exactly
  when required, without repatching the already-prepared module loader.
- **`mass:` is `mass0:`.** iomanX's `parsefile` reads a missing unit number as 0, and libcglue
  passes both spellings through unchanged. bdmfs_fatfs serves the N-th FAT/exFAT volume it
  mounted as `massN:` (N < 10, FatFs `FF_VOLUMES`): one volume per partition, in mount order,
  with GPT's EFI and MS-reserved partitions skipped. usbmass_bd drives two USB drives at once.
  The boot probes all ten units.
- **Writing a file on USB: only `close()` puts it on the drive.** FatFs keeps a file's last
  partial sector in its own buffer and updates the size in the directory entry only on
  `f_sync`/`f_close`. bdmfs_fatfs's `sync` op returns EIO and libcglue's `fsync()` is
  ENOSYS, so the only flush is to close the file. A file held open across a power cycle can
  come back empty. BDM's block cache is write-through. `debug/log_file.cpp` opens, appends
  and closes on every write for this reason. On a real console over USB that costs **~23 ms
  per logged line** (2026-10-07). A load-trace run's base1 load logged ~1040 lines, about
  24 s of its 76 s, so hardware load times with the log on mostly measure the log.
- Console boot finds the ELF's folder from `argv[0]`. Loaders spell the device their own way
  (`mass:/dir/quake2.elf`, `mass:dir/quake2.elf`, `hdd0:__common:pfs:/...`), and the IOP reset
  renumbers USB drives. Preserve the HDD partition separately, and drop the partition's
  path component for browser-style `hdd0:/partition/dir/elf` paths. Bare `pfsN:` paths need
  partition enumeration after reset. Search folders before roots. PCSX2 passes `host:` plus
  the ELF's absolute host path. The folder is capped
  at 50 chars: the engine builds `<base>/baseq2/<name>` in `MAX_OSPATH` (128) with names up to
  `MAX_QPATH - 1`, so the base path (`massN:` + folder) must stay within 56.
- `rom0:FILEIO`'s `remove()` RPC handler lacks a `break`: every `remove(path)` on the fio
  backend also runs `mkdir(path)`. `sbv_patch_fileio()` fixes it (applied on the host: path;
  `ps2::sys::FileIoRemovePatched()` reports it). fio has no `rename()` (`ENOSYS`). The USB
  boot uses fileXio, which has neither problem.
- The pad uses rom0 SIO2MAN + PADMAN, so cards use **rom0 MCMAN + MCSERV** (same family).
  ps2sdk's own mcman.irx needs its own sio2man and won't link. Load ROM modules through
  `ps2::sys::LoadRomModuleOnce`, since SIO2MAN must not load twice. The same goes for any IRX:
  keyboard and audio bring-ups are one-shot.
- **`mcInit` spins forever if MCSERV isn't running** (a SifBindRpc loop). Call it only after
  every module load succeeded.
- libmc with the old MCSERV: no `mcRename`. "Unformatted" comes from `mcGetInfo`'s result
  (-2), and the first `mcGetInfo` after insertion returns -1. `mcOpen` takes **IOP flags**
  (`FIO_O_RDONLY` 1, `WRONLY` 2, create `0x200`), not newlib `O_*`. There is no `O_TRUNC`, so
  delete then create. At most 3 open handles.
- The PS2 BIOS browser ignores the icon texture's alpha bit: transparent texels draw black.
