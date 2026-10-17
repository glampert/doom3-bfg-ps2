// ================================================================================================
// File: swf_execution.h
// Brief: Validate the admitted native timeline and AVM1 grammar before enabling execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui
{
static constexpr std::uint32_t kMaxSwfActionBytes = 65535;
static constexpr std::uint32_t kMaxSwfActions = 65536;
static constexpr std::uint32_t kMaxSwfFunctions = 256;
static constexpr std::uint32_t kMaxSwfFunctionDepth = 16;
static constexpr std::uint32_t kMaxSwfSpriteDepth = 16;
static constexpr std::uint32_t kMaxSwfDisplayDepth = 1024;

struct SwfExecutionSummary
{
    std::uint32_t actionBlocks = 0;
    std::uint32_t actions = 0;
    std::uint32_t functions = 0;
    std::uint32_t placements = 0;
    std::uint32_t removals = 0;
    std::uint32_t maxFunctionDepth = 0;
};

struct SwfPlacement
{
    std::uint16_t depth = 0;
    std::uint16_t character = 0;
    std::uint16_t clipDepth = 0;
    bool move = false;
    bool hasCharacter = false;
    bool remove = false;
};

// Counts accumulate into output only on success. This admits the action-family union used by the
// authored demo and selected retail shell/dialog, with checked references/operands. It does not
// enable the interpreter's other APIs. Runtime stack/call/instruction limits remain mandatory. Function
// bodies may use the legacy native encoding outside ActionLength, but cannot be branch targets.
bool ValidateSwfActions(const void * bytes, std::size_t length, std::uint32_t frames,
                        SwfExecutionSummary & output);

// Dictionary kinds match BSWF16. Only image/shape/sprite/edittext placements are executable.
// Unsupported clip actions, filters, classes and bitmap caching are rejected explicitly.
// Placement output is zero for DoAction. Outputs are unchanged on failure.
bool ValidateSwfCommand(std::uint32_t tag, const void * bytes, std::size_t length,
                        const std::uint8_t * dictionaryTypes, std::uint32_t dictionaryCount,
                        std::uint32_t frames, SwfExecutionSummary & output,
                        SwfPlacement * placement = nullptr);
} // namespace ps2::ui
