// ================================================================================================
// File: codec_memory.cpp
// Brief: Keep codec allocations in the tagged heap without temporary files or unchecked products.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <cstdio>
#include <limits>
#include <zlib/zlib.h>
#define JPEG_INTERNALS
#include <jpeg-6/jpeglib.h>

// jmemsys.h requires the private declarations established by jpeglib.h above.
#include <jpeg-6/jmemsys.h>

namespace
{
enum class EngineTag : std::uint16_t
{
#define MEM_TAG(x) x,
#include <idlib/sys/sys_alloc_tags.h>
#undef MEM_TAG
};
static constexpr std::uint16_t kJpegTag = static_cast<std::uint16_t>(EngineTag::JPG);
static constexpr std::uint16_t kZipTag = static_cast<std::uint16_t>(EngineTag::ZIP);
} // namespace

// These typed hooks replace jmemnobs.cpp; the codec handles a null allocation through its error exit.
void * jpeg_get_small(j_common_ptr, size_t bytes) { return ps2::heap::TryAlloc(bytes, kJpegTag); }
void * jpeg_get_large(j_common_ptr, size_t bytes) { return ps2::heap::TryAlloc(bytes, kJpegTag); }
void jpeg_free_small(j_common_ptr, void * pointer, size_t) { ps2::heap::Free(pointer); }
void jpeg_free_large(j_common_ptr, void * pointer, size_t) { ps2::heap::Free(pointer); }
long jpeg_mem_available(j_common_ptr, long, long maximum, long) { return maximum; }
long jpeg_mem_init(j_common_ptr) { return 0; }
void jpeg_mem_term(j_common_ptr) {}
PS2_COLD_FUNC void jpeg_open_backing_store(j_common_ptr decoder, backing_store_ptr, long)
{
    ERREXIT(decoder, JERR_NO_BACKING_STORE);
}

// MY_ZCALLOC excludes zutil.c's malloc/free defaults. Engine-supplied stream callbacks still win.
extern "C"
{
    voidpf zcalloc(voidpf, unsigned int items, unsigned int size)
    {
        const size_t count = static_cast<size_t>(items);
        const size_t stride = static_cast<size_t>(size);
        if (stride != 0 && count > std::numeric_limits<size_t>::max() / stride)
        {
            return nullptr;
        }
        return ps2::heap::TryAlloc(count * stride, kZipTag);
    }
    void zcfree(voidpf, voidpf pointer) { ps2::heap::Free(pointer); }
}

// The engine-modified JPEG error module uses these hooks in its default error manager.
[[noreturn]] PS2_COLD_FUNC PS2_PRINTF_FUNC(1, 2) void jpg_Error(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    ps2::FatalErrorV(format, args);
}
PS2_PRINTF_FUNC(1, 2)
void jpg_Printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    ps2::LogV(ps2::LogLevel::Warning, format, args);
    va_end(args);
}
