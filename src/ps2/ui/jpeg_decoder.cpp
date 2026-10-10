// ================================================================================================
// File: jpeg_decoder.cpp
// Brief: Keep malformed SWF images inside their input bounds and release decoder/output state on failure.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/jpeg_decoder.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <cstdio>
#include <cstring>
#include <jpeg-6/jpeglib.h>

namespace ps2::jpeg
{
namespace
{

static constexpr unsigned int kMaxDimension = 1024;
static constexpr size_t kMaxOutputBytes = 4U * 1024U * 1024U;
enum class EngineTag : std::uint16_t
{
#define MEM_TAG(x) x,
#include <idlib/sys/sys_alloc_tags.h>
#undef MEM_TAG
};
static constexpr std::uint16_t kImageTag = static_cast<std::uint16_t>(EngineTag::SWF);

struct State;
struct ErrorManager : jpeg_error_mgr
{
    State * owner;
};
struct State
{
    jpeg_decompress_struct decoder;
    ErrorManager error;
    jpeg_source_mgr source;
    unsigned char * output;
};

State * Owner(j_common_ptr decoder)
{
    return static_cast<ErrorManager *>(decoder->err)->owner;
}

void Release(State * state)
{
    if (state == nullptr) { return; }
    heap::Free(state->output);
    jpeg_destroy_decompress(&state->decoder);
    heap::Free(state);
}

[[noreturn]] PS2_COLD_FUNC void Fail(State * state, const char * reason)
{
    // libjpeg's formatted reason may point into the state being released.
    char message[JMSG_LENGTH_MAX] = {};
    std::snprintf(message, sizeof(message), "%s", reason);
    Release(state);
    FatalError("SWF JPEG: %s", message);
}

PS2_COLD_FUNC void ErrorExit(j_common_ptr decoder)
{
    char message[JMSG_LENGTH_MAX] = {};
    decoder->err->format_message(decoder, message);
    Fail(Owner(decoder), message);
}

void OutputMessage(j_common_ptr decoder)
{
    char message[JMSG_LENGTH_MAX] = {};
    decoder->err->format_message(decoder, message);
    Log(LogLevel::Warning, "SWF JPEG: %s", message);
}

void InitSource(j_decompress_ptr) {}
void TermSource(j_decompress_ptr) {}
boolean FillInput(j_decompress_ptr decoder)
{
    // Memory input is complete. Returning TRUE without adding bytes lets libjpeg read past EOF.
    Fail(Owner(reinterpret_cast<j_common_ptr>(decoder)), "truncated input");
}
void SkipInput(j_decompress_ptr decoder, long count)
{
    if (count <= 0) { return; }
    const size_t bytes = static_cast<size_t>(count);
    if (bytes > decoder->src->bytes_in_buffer)
    {
        Fail(Owner(reinterpret_cast<j_common_ptr>(decoder)), "marker extends past input");
    }
    decoder->src->next_input_byte += bytes;
    decoder->src->bytes_in_buffer -= bytes;
}

} // namespace

void * Create()
{
    auto * state = static_cast<State *>(heap::Alloc(sizeof(State), kImageTag));
    *state = {};
    state->error.owner = state;
    state->decoder.err = jpeg_std_error(&state->error);
    state->error.error_exit = ErrorExit;
    state->error.output_message = OutputMessage;
    jpeg_create_decompress(&state->decoder);
    return state;
}

void Destroy(void * decoder)
{
    Release(static_cast<State *>(decoder));
}

unsigned char * Decode(void * decoder, const unsigned char * input, int inputSize, int & width, int & height)
{
    auto * state = static_cast<State *>(decoder);
    width = 0;
    height = 0;
    if (state == nullptr || input == nullptr || inputSize <= 0)
    {
        Fail(state, "invalid input");
    }
    jpeg_decompress_struct & info = state->decoder;
    state->source = {};
    state->source.next_input_byte = input;
    state->source.bytes_in_buffer = static_cast<size_t>(inputSize);
    state->source.init_source = InitSource;
    state->source.fill_input_buffer = FillInput;
    state->source.skip_input_data = SkipInput;
    state->source.resync_to_restart = jpeg_resync_to_restart;
    state->source.term_source = TermSource;
    info.src = &state->source;
    int result;
    do
    {
        result = jpeg_read_header(&info, FALSE);
        if (result == JPEG_HEADER_TABLES_ONLY && info.src->bytes_in_buffer == 0)
        {
            info.src = nullptr;
            return nullptr;
        }
    } while (result == JPEG_HEADER_TABLES_ONLY);
    if (result != JPEG_HEADER_OK || info.image_width == 0 || info.image_height == 0 ||
        info.image_width > kMaxDimension || info.image_height > kMaxDimension)
    {
        Fail(state, "invalid or oversized dimensions (limit 1024)");
    }
    info.out_color_space = JCS_RGB;
    info.dct_method = JDCT_DEFAULT;
    if (!jpeg_start_decompress(&info) || info.output_components != 4)
    {
        Fail(state, "unsupported output format");
    }
    const size_t rowBytes = static_cast<size_t>(info.output_width) * 4U;
    const size_t outputBytes = rowBytes * static_cast<size_t>(info.output_height);
    if (outputBytes == 0 || outputBytes > kMaxOutputBytes)
    {
        Fail(state, "decoded image exceeds 4 MiB");
    }
    state->output = static_cast<unsigned char *>(heap::TryAlloc(outputBytes, kImageTag));
    if (state->output == nullptr) { Fail(state, "output allocation failed"); }
    std::memset(state->output, 255, outputBytes);
    while (info.output_scanline < info.output_height)
    {
        JSAMPROW row = state->output + static_cast<size_t>(info.output_scanline) * rowBytes;
        if (jpeg_read_scanlines(&info, &row, 1) != 1) { Fail(state, "scanline did not advance"); }
    }
    if (!jpeg_finish_decompress(&info)) { Fail(state, "incomplete image"); }
    width = static_cast<int>(info.output_width);
    height = static_cast<int>(info.output_height);
    info.src = nullptr;
    unsigned char * output = state->output;
    state->output = nullptr;
    return output;
}

} // namespace ps2::jpeg
