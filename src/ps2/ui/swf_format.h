// ================================================================================================
// File: swf_format.h
// Brief: Preflight retained BSWF16 allocations for resource-only native loading, without AVM1 execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/swf_execution.h"
#include <cstddef>
#include <cstdint>

namespace ps2::ui::swf
{
static constexpr std::size_t kMaxBytes = 1024U * 1024U;

struct Summary
{
    std::uint32_t dictionary = 0;
    std::uint32_t sprites = 0;
    std::uint32_t commands = 0;
    std::uint32_t vertices = 0;
    std::uint32_t indices = 0;
    std::uint32_t fonts = 0;
    std::uint32_t editTexts = 0;
    std::uint32_t previewText = 0;
    // Shape outline records; native SWF draws them as line-mode strokes, which the 2D backend lacks.
    std::uint32_t strokes = 0;
};

// Names are transient NUL-terminated ASCII. Returning false rejects the resource before native parsing.
using IdentityCheck = bool (*)(const char * name, bool font, void * context);

// This validates storage/counts and admitted names, NOT scripts or placement semantics. Its snapshots
// may only enter a resource-only idSWF, which never runs timelines, AVM1, events or full movie drawing.
// On failure output is untouched. No allocation or recursion is used.
bool ValidateBytes(const void * bytes, std::size_t length, Summary & output,
                      IdentityCheck identity = nullptr, void * context = nullptr);
// Additional execution admission: decode every command/action after all dictionary kinds are known,
// reject invalid baseline display mutations and cyclic/deep sprite placement graphs. No allocation
// is performed. Rendering features and dynamic runtime budgets remain separately gated.
bool ValidateExecutionBytes(const void * bytes, std::size_t length, ExecutionSummary & output);
// Check the native one-entry-per-line dictionary grammar before its legacy parser. UTF-8 validity
// is checked separately by native VerifyUTF8 in every configuration. Counts publish only on success.
bool ValidateEnglishBytes(const void * bytes, std::size_t length, std::uint32_t & entries);
} // namespace ps2::ui::swf
