// ================================================================================================
// File: jpeg_decoder_tests.cpp
// Brief: Exercise the real shipped JPEG codec and verify explicit failure cleanup under sanitizers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/jpeg_decoder.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jpeg-6/jpeglib.h>

namespace
{
static ps2::heap::Stats s_baseline = {};

bool LedgerRestored()
{
    const ps2::heap::Stats now = ps2::heap::GetTotalStats();
    return now.requestedBytes == s_baseline.requestedBytes && now.backingBytes == s_baseline.backingBytes &&
        now.allocationCount == s_baseline.allocationCount;
}

size_t MakeFixture(unsigned char * data, size_t capacity)
{
    FILE * file = std::tmpfile();
    if (file == nullptr) { std::exit(4); }
    jpeg_compress_struct info = {};
    jpeg_error_mgr error = {};
    info.err = jpeg_std_error(&error);
    jpeg_create_compress(&info);
    jpeg_stdio_dest(&info, file);
    info.image_width = 2;
    info.image_height = 2;
    info.input_components = 4;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 95, TRUE);
    jpeg_start_compress(&info, TRUE);
    unsigned char pixels[] = {200, 40, 30, 255, 200, 40, 30, 255};
    while (info.next_scanline < info.image_height)
    {
        JSAMPROW row = pixels;
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    std::rewind(file);
    const size_t size = std::fread(data, 1, capacity, file);
    if (size == capacity || std::ferror(file) != 0) { std::exit(4); }
    std::fclose(file);
    return size;
}

bool PixelsMatch(const unsigned char * data, int width, int height)
{
    if (data == nullptr || width != 2 || height != 2) { return false; }
    for (size_t i = 0; i < 16; i += 4)
    {
        if (data[i] < 190 || data[i] > 210 || data[i + 1] < 30 || data[i + 1] > 50 ||
            data[i + 2] < 20 || data[i + 2] > 40 || data[i + 3] != 255) { return false; }
    }
    return true;
}

bool SeparateTables(const unsigned char * data, size_t size, unsigned char * tables, size_t & tableSize,
    unsigned char * abbreviated, size_t & abbreviatedSize)
{
    std::memcpy(tables, data, 2);
    std::memcpy(abbreviated, data, 2);
    tableSize = 2;
    abbreviatedSize = 2;
    size_t offset = 2;
    while (offset + 4 <= size)
    {
        if (data[offset] != 255) { return false; }
        const unsigned char marker = data[offset + 1];
        if (marker == 0xda)
        {
            std::memcpy(abbreviated + abbreviatedSize, data + offset, size - offset);
            abbreviatedSize += size - offset;
            tables[tableSize++] = 255;
            tables[tableSize++] = 0xd9;
            return true;
        }
        const size_t length = (static_cast<size_t>(data[offset + 2]) << 8U) + data[offset + 3] + 2U;
        if (length < 4 || length > size - offset) { return false; }
        if (marker == 0xdb || marker == 0xc4)
        {
            std::memcpy(tables + tableSize, data + offset, length);
            tableSize += length;
        }
        else
        {
            std::memcpy(abbreviated + abbreviatedSize, data + offset, length);
            abbreviatedSize += length;
        }
        offset += length;
    }
    return false;
}
} // namespace

// The test sink inspects the ledger after the adapter's cleanup. Production uses log.cpp's
// terminating sink; its forwarding/termination behavior has separate subprocess regressions.
namespace ps2
{
void LogV(LogLevel, const char * format, va_list args) { std::vprintf(format, args); std::printf("\n"); }
void Log(LogLevel level, const char * format, ...)
{
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}
[[noreturn]] void FatalErrorV(const char * format, va_list args)
{
    LogV(LogLevel::Fatal, format, args);
    const bool restored = LedgerRestored();
    Log(LogLevel::Info, "JPEG cleanup %s", restored ? "PASS" : "FAIL");
    std::exit(restored ? 2 : 3);
}
[[noreturn]] void FatalError(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    FatalErrorV(format, args);
}
} // namespace ps2

int main(int argc, char ** argv)
{
    if (argc != 2 && argc != 3) { return 4; }
    unsigned char data[2048] = {};
    const size_t size = MakeFixture(data, sizeof(data));
    if (argc == 3 && std::strcmp(argv[1], "--write-fixture") == 0)
    {
        FILE * file = std::fopen(argv[2], "wb");
        if (file == nullptr) { return 4; }
        const bool written = std::fwrite(data, 1, size, file) == size;
        const bool closed = std::fclose(file) == 0;
        return written && closed ? 0 : 4;
    }
    s_baseline = ps2::heap::GetTotalStats();
    void * decoder = ps2::jpeg::Create();
    int width = -1;
    int height = -1;
    if (std::strcmp(argv[1], "valid") == 0)
    {
        for (int i = 0; i < 3; ++i)
        {
            unsigned char * output = ps2::jpeg::Decode(decoder, data, static_cast<int>(size), width, height);
            const bool match = PixelsMatch(output, width, height);
            ps2::heap::Free(output);
            if (!match) { return 5; }
        }
        unsigned char tables[2048] = {};
        unsigned char abbreviated[2048] = {};
        size_t tableSize = 0;
        size_t abbreviatedSize = 0;
        if (!SeparateTables(data, size, tables, tableSize, abbreviated, abbreviatedSize)) { return 6; }
        if (ps2::jpeg::Decode(decoder, tables, static_cast<int>(tableSize), width, height) != nullptr ||
            width != 0 || height != 0) { return 7; }
        unsigned char * output = ps2::jpeg::Decode(decoder, abbreviated, static_cast<int>(abbreviatedSize), width, height);
        const bool match = PixelsMatch(output, width, height);
        ps2::heap::Free(output);
        ps2::jpeg::Destroy(decoder);
        if (!match || !LedgerRestored()) { return 8; }
        ps2::Log(ps2::LogLevel::Info, "JPEG valid/reuse/tables/alpha/ledger PASS");
        return 0;
    }
    if (std::strcmp(argv[1], "invalid") == 0)
    {
        ps2::jpeg::Decode(decoder, nullptr, 0, width, height);
    }
    else if (std::strcmp(argv[1], "marker") == 0)
    {
        const unsigned char malformed[] = {255, 0xd8, 255, 0xe1, 255, 255};
        ps2::jpeg::Decode(decoder, malformed, sizeof(malformed), width, height);
    }
    else if (std::strcmp(argv[1], "oversize") == 0)
    {
        for (size_t i = 0; i + 8 < size; ++i)
        {
            if (data[i] == 255 && data[i + 1] == 0xc0) { data[i + 7] = 4; data[i + 8] = 1; break; }
        }
        ps2::jpeg::Decode(decoder, data, static_cast<int>(size), width, height);
    }
    else if (std::strcmp(argv[1], "truncated") == 0)
    {
        ps2::jpeg::Decode(decoder, data, static_cast<int>(size - 2), width, height);
    }
    else if (std::strcmp(argv[1], "signature") == 0)
    {
        data[0] = 0;
        ps2::jpeg::Decode(decoder, data, static_cast<int>(size), width, height);
    }
    return 9;
}
