// ================================================================================================
// File: iop_modules.S
// Brief: The IOP storage drivers a console boot starts once it has reset the IOP, as the SDK built
//        them. They go in the boot data (config/linkfile.ld), which the heap takes back once the
//        boot has started them: the drive they read is out of reach until they run, so they can't
//        be loose files on it. Every driver started after the boot is a loose file in irx/.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The Makefile assembles this with the SDK's iop/irx directory on the include path. Each module
// gets <name>_irx and <name>_irx_end, read by iop_boot.cpp.

    .section .bootdata, "a"

    // The heap's block header for the boot data takes its first 16 bytes (heap_allocator.h).
    .space 64

    .macro module name
    // SifExecModuleBuffer DMAs the image to the IOP: whole cache lines, so it starts on one.
    .balign 64
    .globl \name\()_irx
\name\()_irx:
    .incbin "\name\().irx"
    .globl \name\()_irx_end
\name\()_irx_end:
    .endm

    // Every console boot: iomanX and fileXio route the C library's files to the drives.
    module iomanX
    module fileXio

    // An APA/PFS hard disk (DEV9, ATA, APA, PFS).
    module ps2dev9
    module ps2atad
    module ps2hdd
    module ps2fs

    // USB mass storage (BDM with FAT/exFAT, the USB stack, the mass storage block device).
    module bdm
    module bdmfs_fatfs
    module usbd
    module usbmass_bd

    .balign 64
