// ================================================================================================
// File: swf_execution.h
// Brief: Validate the admitted native timeline and AVM1 grammar before enabling execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui::swf
{
static constexpr std::uint32_t kMaxActionBytes = 65535;
static constexpr std::uint32_t kMaxActions = 65536;
static constexpr std::uint32_t kMaxFunctions = 256;
static constexpr std::uint32_t kMaxFunctionDepth = 16;
static constexpr std::uint32_t kMaxSpriteDepth = 16;
static constexpr std::uint32_t kMaxDisplayDepth = 1024;

struct ExecutionSummary
{
    std::uint32_t actionBlocks = 0;
    std::uint32_t actions = 0;
    std::uint32_t functions = 0;
    std::uint32_t placements = 0;
    std::uint32_t removals = 0;
    std::uint32_t maxFunctionDepth = 0;
    // Draw features: screen blending needs channel CLUTs, and an additive alpha term has no 2D path.
    std::uint32_t masks = 0;
    std::uint32_t screenBlends = 0;
    std::uint32_t additiveColors = 0;
    std::uint32_t additiveAlphas = 0;
};

struct Placement
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
bool ValidateActions(const void * bytes, std::size_t length, std::uint32_t frames,
                        ExecutionSummary & output);

// Dictionary kinds match BSWF16. Only image/shape/sprite/edittext placements are executable.
// Unsupported clip actions, filters, classes and bitmap caching are rejected explicitly.
// Placement output is zero for DoAction. Outputs are unchanged on failure.
bool ValidateCommand(std::uint32_t tag, const void * bytes, std::size_t length,
                        const std::uint8_t * dictionaryTypes, std::uint32_t dictionaryCount,
                        std::uint32_t frames, ExecutionSummary & output,
                        Placement * placement = nullptr);
} // namespace ps2::ui::swf
