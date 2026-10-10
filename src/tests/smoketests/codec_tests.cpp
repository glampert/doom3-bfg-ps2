// ================================================================================================
// File: codec_tests.cpp
// Brief: Decode independent authored streams and verify errors, tagged allocation and repeated cleanup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/codec_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/jpeg_decoder.h"
#include "tests/smoketests/codec_fixtures.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <zlib/zlib.h>

extern "C" voidpf zcalloc(voidpf, unsigned int items, unsigned int size);
extern "C" void zcfree(voidpf, voidpf pointer);

namespace ps2::smoketests
{
namespace
{
using namespace fixtures;

bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK codecs/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool Restored(const heap::Stats & before)
{
    const heap::Stats after = heap::GetTotalStats();
    return before.requestedBytes == after.requestedBytes && before.backingBytes == after.backingBytes &&
           before.allocationCount == after.allocationCount;
}

// Feed small independent chunks to exercise input/output suspension and sliding-window ownership.
bool InflateFixture(const unsigned char * input, size_t bytes, int windowBits)
{
    const heap::Stats before = heap::GetTotalStats();
    z_stream stream = {};
    if (inflateInit2(&stream, windowBits) != Z_OK)
    {
        return false;
    }
    const bool owned = heap::GetTotalStats().allocationCount > before.allocationCount;
    unsigned char output[sizeof(kZlibMessage) + 8] = {};
    size_t supplied = 0;
    int result = Z_OK;
    for (int step = 0; step < 256 && result == Z_OK; ++step)
    {
        if (stream.avail_in == 0 && supplied < bytes)
        {
            const size_t chunk = std::min<size_t>(3, bytes - supplied);
            stream.next_in = const_cast<Bytef *>(input + supplied);
            stream.avail_in = static_cast<uInt>(chunk);
            supplied += chunk;
        }
        stream.next_out = output + static_cast<size_t>(stream.total_out);
        stream.avail_out = static_cast<uInt>(std::min<size_t>(5, sizeof(output) - static_cast<size_t>(stream.total_out)));
        result = inflate(&stream, Z_NO_FLUSH);
    }
    const bool decoded = result == Z_STREAM_END && stream.total_in == bytes &&
                         stream.total_out == sizeof(kZlibMessage) &&
                         std::memcmp(output, kZlibMessage, sizeof(kZlibMessage)) == 0;
    const bool ended = inflateEnd(&stream) == Z_OK;
    return owned && decoded && ended && Restored(before);
}

bool DeflateRoundTrip(int windowBits)
{
    const heap::Stats before = heap::GetTotalStats();
    z_stream stream = {};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }
    unsigned char encoded[256] = {};
    stream.next_in = const_cast<Bytef *>(kZlibMessage);
    stream.avail_in = static_cast<uInt>(sizeof(kZlibMessage));
    stream.next_out = encoded;
    stream.avail_out = sizeof(encoded);
    const bool compressed = deflate(&stream, Z_FINISH) == Z_STREAM_END;
    const size_t bytes = static_cast<size_t>(stream.total_out);
    const bool ended = deflateEnd(&stream) == Z_OK;
    return compressed && ended && InflateFixture(encoded, bytes, windowBits) && Restored(before);
}

bool RejectedStream(const unsigned char * input, size_t bytes, int expected)
{
    const heap::Stats before = heap::GetTotalStats();
    z_stream stream = {};
    if (inflateInit(&stream) != Z_OK)
    {
        return false;
    }
    unsigned char output[sizeof(kZlibMessage) + 8] = {};
    stream.next_in = const_cast<Bytef *>(input);
    stream.avail_in = static_cast<uInt>(bytes);
    stream.next_out = output;
    stream.avail_out = sizeof(output);
    const int result = inflate(&stream, Z_FINISH);
    const bool ended = inflateEnd(&stream) == Z_OK;
    return result == expected && ended && Restored(before);
}

struct FailingAllocator
{
    unsigned int calls = 0;
    unsigned int failAt = 0;
    unsigned int live = 0;
};
voidpf Allocate(voidpf opaque, uInt items, uInt size)
{
    auto * state = static_cast<FailingAllocator *>(opaque);
    if (++state->calls == state->failAt)
    {
        return nullptr;
    }
    void * pointer = zcalloc(nullptr, items, size);
    if (pointer != nullptr)
    {
        ++state->live;
    }
    return pointer;
}
void Free(voidpf opaque, voidpf pointer)
{
    auto * state = static_cast<FailingAllocator *>(opaque);
    PS2_Assert(state->live > 0);
    --state->live;
    zcfree(nullptr, pointer);
}
bool AllocationFailures()
{
    const heap::Stats before = heap::GetTotalStats();
    for (unsigned int fail = 1; fail <= 5; ++fail)
    {
        FailingAllocator allocator;
        allocator.failAt = fail;
        z_stream stream = {};
        stream.opaque = &allocator;
        stream.zalloc = Allocate;
        stream.zfree = Free;
        const int result = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
        if (result == Z_OK)
        {
            deflateEnd(&stream);
        }
        if (result != Z_MEM_ERROR || allocator.live != 0 || !Restored(before))
        {
            return false;
        }
    }
    // Inflation acquires its window lazily, after initialization, when a small output chunk fills.
    for (unsigned int fail = 1; fail <= 2; ++fail)
    {
        FailingAllocator allocator;
        allocator.failAt = fail;
        z_stream stream = {};
        stream.opaque = &allocator;
        stream.zalloc = Allocate;
        stream.zfree = Free;
        int result = inflateInit(&stream);
        if (result == Z_OK)
        {
            unsigned char output[5] = {};
            stream.next_in = const_cast<Bytef *>(kZlibWrapped);
            stream.avail_in = sizeof(kZlibWrapped);
            stream.next_out = output;
            stream.avail_out = sizeof(output);
            result = inflate(&stream, Z_NO_FLUSH);
            if (inflateEnd(&stream) != Z_OK)
            {
                return false;
            }
        }
        if (result != Z_MEM_ERROR || allocator.live != 0 || !Restored(before))
        {
            return false;
        }
    }
    void * oversized = zcalloc(nullptr, std::numeric_limits<unsigned int>::max(), std::numeric_limits<unsigned int>::max());
    const bool rejected = oversized == nullptr;
    zcfree(nullptr, oversized);
    return rejected && Restored(before);
}

bool PixelsMatch(const unsigned char * output, int width, int height)
{
    if (output == nullptr || width != 2 || height != 2)
    {
        return false;
    }
    for (size_t offset = 0; offset < 16; offset += 4)
    {
        if (output[offset] < 190 || output[offset] > 210 || output[offset + 1] < 30 || output[offset + 1] > 50 ||
            output[offset + 2] < 20 || output[offset + 2] > 40 || output[offset + 3] != 255)
        {
            return false;
        }
    }
    return true;
}
bool JpegFixture()
{
    const heap::Stats before = heap::GetTotalStats();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        void * decoder = jpeg::Create();
        int width = -1;
        int height = -1;
        unsigned char * output = jpeg::Decode(decoder, kJpeg, sizeof(kJpeg), width, height);
        bool decoded = PixelsMatch(output, width, height);
        heap::Free(output);
        output = jpeg::Decode(decoder, kJpegTables, sizeof(kJpegTables), width, height);
        decoded = decoded && output == nullptr && width == 0 && height == 0;
        output = jpeg::Decode(decoder, kJpegAbbreviated, sizeof(kJpegAbbreviated), width, height);
        decoded = decoded && PixelsMatch(output, width, height);
        heap::Free(output);
        jpeg::Destroy(decoder);
        if (!decoded || !Restored(before))
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool RunCodecTests()
{
    const auto * checksum = reinterpret_cast<const Bytef *>("123456789");
    bool passed = Check("crc-known-vector", crc32(0, checksum, 9) == 0xcbf43926UL);
    bool streaming = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        streaming = InflateFixture(kZlibWrapped, sizeof(kZlibWrapped), MAX_WBITS) &&
                    InflateFixture(kZlibRaw, sizeof(kZlibRaw), -MAX_WBITS) &&
                    DeflateRoundTrip(MAX_WBITS) && DeflateRoundTrip(-MAX_WBITS) && streaming;
    }
    passed = Check("zlib-streaming-ledger", streaming) && passed;
    unsigned char corrupt[sizeof(kZlibWrapped)];
    std::memcpy(corrupt, kZlibWrapped, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1] ^= 1;
    passed = Check("zlib-error-cleanup", RejectedStream(corrupt, sizeof(corrupt), Z_DATA_ERROR) &&
                                         RejectedStream(kZlibWrapped, sizeof(kZlibWrapped) - 1, Z_BUF_ERROR) &&
                                         AllocationFailures()) &&
             passed;
    passed = Check("jpeg-rgba-tables-ledger", JpegFixture()) && passed;
    return passed;
}

bool RunCodecFailureProbe(const char * name)
{
    // Do not acquire a decoder for unrelated probes.
    if (std::strncmp(name, "jpeg-", 5) != 0)
    {
        return false;
    }
    void * decoder = jpeg::Create();
    int width = -1;
    int height = -1;
    if (std::strcmp(name, "jpeg-invalid") == 0)
    {
        jpeg::Decode(decoder, nullptr, 0, width, height);
    }
    else if (std::strcmp(name, "jpeg-marker") == 0)
    {
        const unsigned char malformed[] = { 255, 0xd8, 255, 0xe1, 255, 255 };
        jpeg::Decode(decoder, malformed, sizeof(malformed), width, height);
    }
    else if (std::strcmp(name, "jpeg-truncated") == 0)
    {
        jpeg::Decode(decoder, kJpeg, sizeof(kJpeg) - 2, width, height);
    }
    else if (std::strcmp(name, "jpeg-oversize") == 0)
    {
        unsigned char oversized[sizeof(kJpeg)];
        std::memcpy(oversized, kJpeg, sizeof(oversized));
        for (size_t offset = 0; offset + 8 < sizeof(oversized); ++offset)
        {
            if (oversized[offset] == 255 && oversized[offset + 1] == 0xc0)
            {
                oversized[offset + 7] = 4;
                oversized[offset + 8] = 1;
                break;
            }
        }
        jpeg::Decode(decoder, oversized, sizeof(oversized), width, height);
    }
    else if (std::strcmp(name, "jpeg-signature") == 0)
    {
        const unsigned char malformed[] = { 0, 0, 0, 0 };
        jpeg::Decode(decoder, malformed, sizeof(malformed), width, height);
    }
    else
    {
        jpeg::Destroy(decoder);
        return false;
    }
    jpeg::Destroy(decoder);
    return true; // The runner rejects an expected fatal returning normally.
}
} // namespace ps2::smoketests
