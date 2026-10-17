// ================================================================================================
// File: swf_execution_tests.cpp
// Brief: Sanitizer coverage for executable SWF operands, branches, functions and display references.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/swf_execution.h"
#include "ps2/ui/swf_format.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace
{
using U8 = std::uint8_t;
using U32 = std::uint32_t;
using Bytes = std::vector<U8>;
using Command = std::pair<U32, Bytes>;
static constexpr U8 kTypes[] = { 0, 1, 2, 4, 5, 7 };

void Number(Bytes & bytes, U32 value, unsigned width, bool big = false)
{
    for (unsigned index = 0; index < width; ++index)
    {
        const unsigned shift = (big ? width - index - 1 : index) * 8;
        bytes.push_back(static_cast<U8>(value >> shift));
    }
}

void String(Bytes & bytes, const char * text)
{
    Number(bytes, static_cast<U32>(std::strlen(text)), 4);
    bytes.insert(bytes.end(), text, text + std::strlen(text));
}

void Float(Bytes & bytes, float value)
{
    U32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Number(bytes, bits, 4, true);
}

Bytes Action(U8 opcode, const Bytes & body)
{
    Bytes result = { opcode };
    Number(result, static_cast<U32>(body.size()), 2);
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

void Append(Bytes & bytes, const Bytes & more)
{
    bytes.insert(bytes.end(), more.begin(), more.end());
}

Bytes Function(const Bytes & code, bool included, bool second = false, U8 registers = 4, U32 flags = 0)
{
    Bytes body = { 'f', 0, 0, 0 };

    if (second)
    {
        body.push_back(registers);
        Number(body, flags, 2);
    }

    Number(body, static_cast<U32>(code.size()), 2);

    if (included)
    {
        Append(body, code);
    }

    Bytes result = Action(second ? 0x8e : 0x9b, body);

    if (!included)
    {
        Append(result, code);
    }

    return result;
}

void RejectActions(const Bytes & bytes)
{
    ps2::ui::SwfExecutionSummary summary;
    summary.actions = 42;
    assert(!ps2::ui::ValidateSwfActions(bytes.data(), bytes.size(), 2, summary));
    assert(summary.actions == 42);
}

void AcceptActions(const Bytes & bytes)
{
    ps2::ui::SwfExecutionSummary summary;
    assert(ps2::ui::ValidateSwfActions(bytes.data(), bytes.size(), 2, summary));
    assert(summary.actions != 0);
}

void Actions()
{
    static constexpr U8 kAdmittedSimple[] = {
        0x00, 0x06, 0x07, 0x0a, 0x0b, 0x0c, 0x12, 0x17, 0x1c, 0x1d, 0x26, 0x3c,
        0x3d, 0x47, 0x48, 0x49, 0x4b, 0x4c, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x67
    };

    // Freeze the scoped API boundary: dormant native interpreter features are not executable.
    for (U32 opcode = 0; opcode < 0x80; ++opcode)
    {
        bool expected = false;

        for (const U8 admitted : kAdmittedSimple)
        {
            expected = expected || opcode == admitted;
        }

        const Bytes action = { static_cast<U8>(opcode) };
        ps2::ui::SwfExecutionSummary summary;
        assert(ps2::ui::ValidateSwfActions(action.data(), action.size(), 2, summary) == expected);
    }

    AcceptActions({ 7, 0 });
    AcceptActions(Action(0x81, { 1, 0 }));
    AcceptActions(Action(0x96, { 0, 'a', 0, 1, 0, 0, 0x80, 0x3f, 2, 3, 4, 3, 5, 1, 7, 42, 0, 0, 0 }));
    AcceptActions(Action(0x96, { 6, 0, 0, 0xf0, 0x3f, 0, 0, 0, 0 }));
    AcceptActions(Action(0x99, { 0, 0 }));
    AcceptActions(Action(0x99, { 0xfb, 0xff })); // Bounded only by the runtime instruction budget.
    RejectActions({ 0xff, 0, 0 });
    RejectActions({ 0x83, 0, 0 }); // GetURL has no admitted native implementation.
    RejectActions({ 0x27 });       // StartDrag has no admitted native implementation.
    RejectActions({ 0, 7 });       // No hidden actions after End.
    RejectActions({ 0x96 });
    RejectActions({ 0x96, 1, 0, 0 });
    RejectActions(Action(0x96, {}));
    RejectActions(Action(0x96, { 10 }));
    RejectActions(Action(0x96, { 0, 'a' }));
    RejectActions(Action(0x96, { 1, 0, 0, 0x80, 0x7f }));
    RejectActions(Action(0x96, { 6, 0, 0, 0xf0, 0x7f, 0, 0, 0, 0 }));
    RejectActions(Action(0x96, { 6, 0, 0, 0xf0, 0x47, 0, 0, 0, 0 })); // Finite double overflows float.
    RejectActions(Action(0x96, { 4, 4 }));
    RejectActions(Action(0x96, { 5, 2 }));
    RejectActions(Action(0x96, { 8, 0 }));
    RejectActions(Action(0x96, { 9, 0, 0 }));
    RejectActions(Action(0x81, { 2, 0 }));
    RejectActions(Action(0x81, { 0 }));
    RejectActions(Action(0x81, { 0, 0, 0 }));
    RejectActions(Action(0x87, { 4 }));
    RejectActions(Action(0x9f, { 4 }));
    RejectActions(Action(0x9f, { 2 }));
    RejectActions(Action(0x8b, { 'x' }));
    RejectActions(Action(0x8b, { 0 })); // SetTarget is not part of the selected movies.
    RejectActions(Action(0x8c, { 'f', 0 }));
    RejectActions(Action(0x9f, { 0 }));
    RejectActions(Action(0x99, { 1, 0 }));
    RejectActions(Action(0x99, { 0xfc, 0xff })); // Jump into its own length operand.
    RejectActions(Action(0x9d, { 0, 0x80 }));

    Bytes pool = Action(0x88, { 1, 0, 'a', 0 });
    Append(pool, Action(0x96, { 8, 0 }));
    AcceptActions(pool);
    Bytes bad = pool;
    bad.back() = 1;
    RejectActions(bad);
    bad = { 7 };
    Append(bad, pool);
    RejectActions(bad);
    RejectActions(Action(0x88, { 1, 0, 'a' }));

    const Bytes body = { 7, 0 };
    AcceptActions(Function(body, true));
    AcceptActions(Function(body, false));
    AcceptActions(Function(body, true, true));
    AcceptActions(Function({}, false));
    AcceptActions(Function(body, false, true, 255));
    RejectActions(Function(body, false, true, 0, 1)); // Preloaded this needs register one.
    RejectActions(Function(body, false, true, 4, 0x8000));
    RejectActions(Function({ 0xff }, false));
    bad = Function(body, false);
    bad.pop_back();
    RejectActions(bad);
    bad = Action(0x9b, { 'f', 0, 17, 0, 0, 0 });
    RejectActions(bad);
    bad = Action(0x8e, { 'f', 0, 1, 0, 1, 0, 0, 2, 'x', 0, 0, 0 });
    RejectActions(bad);

    Bytes nested = body;

    for (U32 depth = 0; depth < ps2::ui::kMaxSwfFunctionDepth; ++depth)
    {
        nested = Function(nested, false);
    }

    AcceptActions(nested);
    RejectActions(Function(nested, false));

    // A parent cannot jump into the external nested body, even when that offset starts an action.
    bad = Action(0x99, { 9, 0 });
    Append(bad, Function(body, false));
    RejectActions(bad);

    Bytes inherited = Action(0x88, { 1, 0, 'a', 0 });
    Append(inherited, Function(Action(0x96, { 8, 0 }), false));
    AcceptActions(inherited);

    Bytes withBody;
    Number(withBody, static_cast<U32>(pool.size()), 2);
    Append(withBody, pool);
    RejectActions(Action(0x94, withBody)); // With is outside the scoped action profile.

    Bytes longString(4096, 'a');
    longString.insert(longString.begin(), 0);
    longString.push_back(0);
    AcceptActions(Action(0x96, longString));
    longString.insert(longString.begin() + 1, 'a');
    RejectActions(Action(0x96, longString));
    AcceptActions(Bytes(ps2::ui::kMaxSwfActionBytes, 7));
    RejectActions(Bytes(ps2::ui::kMaxSwfActionBytes + 1, 7));
    Bytes tooManyFunctions;

    for (U32 index = 0; index < ps2::ui::kMaxSwfFunctions; ++index)
    {
        Append(tooManyFunctions, Function({}, false));
    }

    AcceptActions(tooManyFunctions);
    Append(tooManyFunctions, Function({}, false));
    RejectActions(tooManyFunctions);

    unsigned random = 42;

    for (unsigned mutation = 0; mutation < 3000; ++mutation)
    {
        random = random * 1664525U + 1013904223U;
        bad = inherited;
        bad[random % bad.size()] ^= static_cast<U8>((random >> 24) | 1U);
        ps2::ui::SwfExecutionSummary ignored;
        (void)ps2::ui::ValidateSwfActions(bad.data(), bad.size(), 2, ignored);
    }
}

bool CommandValid(U32 tag, const Bytes & bytes, ps2::ui::SwfPlacement * placement = nullptr)
{
    ps2::ui::SwfExecutionSummary summary;
    return ps2::ui::ValidateSwfCommand(tag, bytes.data(), bytes.size(), kTypes, sizeof(kTypes), 2, summary, placement);
}

void Commands()
{
    const Bytes image = { 2, 1, 0, 1, 0 };
    ps2::ui::SwfPlacement placement;
    assert(CommandValid(26, image, &placement));
    assert(placement.depth == 1 && placement.character == 1 && placement.hasCharacter && !placement.move);
    assert(CommandValid(26, { 1, 1, 0 }));
    assert(CommandValid(26, { 6, 1, 0, 2, 0, 0 })); // Empty identity matrix bit field.
    assert(CommandValid(26, { 10, 1, 0, 2, 0, 0 }));
    assert(CommandValid(26, { 34, 1, 0, 3, 0, 'x', 0 }));
    assert(CommandValid(26, { 66, 1, 0, 2, 0, 2, 0 }));
    assert(CommandValid(70, { 2, 2, 1, 0, 2, 0, 8 }));
    assert(CommandValid(28, { 1, 0 }, &placement));
    assert(placement.remove && placement.depth == 1);
    assert(CommandValid(12, { 7, 0 }, &placement));
    assert(!placement.remove && placement.depth == 0);

    for (std::size_t size = 0; size < image.size(); ++size)
    {
        assert(!CommandValid(26, Bytes(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(size))));
    }

    assert(!CommandValid(99, image));
    assert(!CommandValid(26, { 0, 1, 0 }));
    assert(!CommandValid(26, { 2, 0, 0, 1, 0 }));
    assert(!CommandValid(26, { 2, 1, 4, 1, 0 }));
    assert(!CommandValid(26, { 2, 1, 0, 6, 0 }));
    assert(!CommandValid(26, { 2, 1, 0, 0, 0 }));
    assert(!CommandValid(26, { 2, 1, 0, 4, 0 }));
    assert(!CommandValid(26, { 130, 1, 0, 1, 0 }));
    assert(!CommandValid(26, { 34, 1, 0, 3, 0, 'x' }));
    assert(!CommandValid(26, { 6, 1, 0, 2, 0, 0x80 }));
    assert(!CommandValid(26, { 10, 1, 0, 2, 0, 0x80 }));
    assert(!CommandValid(26, { 66, 1, 0, 2, 0, 1, 0 }));
    assert(!CommandValid(70, { 2, 1, 1, 0, 2, 0 }));
    assert(!CommandValid(70, { 2, 2, 1, 0, 2, 0, 15 }));
    assert(!CommandValid(70, { 2, 4, 1, 0, 2, 0, 1 }));
    assert(!CommandValid(70, { 2, 8, 1, 0, 2, 0 }));
    assert(!CommandValid(28, { 0, 0 }));
    assert(!CommandValid(28, { 1, 0, 0 }));

    ps2::ui::SwfExecutionSummary summary;
    summary.actions = 42;
    placement.depth = 42;
    assert(!ps2::ui::ValidateSwfCommand(26, nullptr, 0, kTypes, sizeof(kTypes), 1, summary, &placement));
    assert(summary.actions == 42 && placement.depth == 42);
}

Bytes Sprite(const std::vector<Command> & commands)
{
    Bytes result;
    Number(result, 1, 2, true);
    Number(result, 2, 4, true);
    Number(result, 0, 4, true);
    Number(result, static_cast<U32>(commands.size()), 4, true);
    Number(result, 0, 4, true);
    Bytes data;

    for (const auto & command : commands)
    {
        Append(data, command.second);
    }

    Number(result, static_cast<U32>(data.size()), 4, true);
    Append(result, data);
    Number(result, static_cast<U32>(commands.size()), 4, true);

    for (const auto & command : commands)
    {
        Number(result, command.first, 4, true);
        Number(result, static_cast<U32>(command.second.size()), 4, true);
    }

    Number(result, 0, 4, true);
    return result;
}

Bytes Movie(const std::vector<Command> & root, const std::vector<Command> & child = {})
{
    Bytes result;
    Number(result, 0x42535710U, 4, true);
    Number(result, 0, 4, true);
    Number(result, 0, 4, true);
    Float(result, 640);
    Float(result, 480);
    Number(result, 1536, 2, true);
    Append(result, Sprite(root));
    Number(result, 5, 4, true);
    Number(result, 0, 4, true);
    Number(result, 1, 4, true);
    String(result, ".");

    for (unsigned axis = 0; axis < 2; ++axis)
    {
        Number(result, 1, 4, true);
        Number(result, 0, 4, true);
    }

    for (unsigned component = 0; component < 4; ++component)
    {
        Float(result, 1);
    }

    Number(result, 4, 4, true);
    Append(result, Sprite(child));
    Number(result, 5, 4, true);
    String(result, "test");
    result.insert(result.end(), 6, 0);
    Number(result, 0, 4, true);
    Number(result, 7, 4, true);
    Float(result, 0);
    Float(result, 0);
    Float(result, 100);
    Float(result, 100);
    Number(result, 0, 4, true);
    Number(result, 3, 2, true);
    Number(result, 400, 2, true);
    result.insert(result.end(), 6, 0);
    Number(result, 0, 4, true);
    result.insert(result.end(), 8, 0);
    String(result, "label");
    String(result, "English");
    return result;
}

void RejectMovie(const Bytes & bytes)
{
    ps2::ui::SwfExecutionSummary summary;
    summary.actions = 0x12345678;
    assert(!ps2::ui::ValidateSwfExecutionBytes(bytes.data(), bytes.size(), summary));
    assert(summary.actions == 0x12345678);
}

void Movies()
{
    const Command image = { 26, { 2, 1, 0, 1, 0 } };
    const Command child = { 26, { 2, 2, 0, 2, 0 } };
    ps2::ui::SwfExecutionSummary summary;
    Bytes bytes = Movie({ image, child, { 12, { 7, 0 } } });
    assert(ps2::ui::ValidateSwfExecutionBytes(bytes.data(), bytes.size(), summary));
    assert(summary.placements == 2 && summary.actions == 2);

    for (std::size_t size = 0; size < bytes.size(); ++size)
    {
        RejectMovie(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)));
    }

    RejectMovie(Movie({ image, image }));
    RejectMovie(Movie({ { 26, { 1, 1, 0 } } }));
    RejectMovie(Movie({ child }, { child }));
    RejectMovie(Movie({ child, { 26, { 3, 2, 0, 1, 0 } } }));
    RejectMovie(Movie({ image, { 26, { 3, 1, 0, 2, 0 } } }));
    RejectMovie(Movie({ { 12, { 0xff } } }));
    RejectMovie(Movie({ { 26, { 2, 1, 0, 3, 0 } } }));
    bytes = Movie({ image, { 28, { 1, 0 } }, image, { 26, { 3, 1, 0, 1, 0 } } });
    assert(ps2::ui::ValidateSwfExecutionBytes(bytes.data(), bytes.size(), summary));
    assert(summary.placements == 3 && summary.removals == 1);

    unsigned random = 42;

    for (unsigned mutation = 0; mutation < 1500; ++mutation)
    {
        random = random * 1664525U + 1013904223U;
        Bytes bad = bytes;
        bad[random % bad.size()] ^= static_cast<U8>((random >> 24) | 1U);
        ps2::ui::SwfExecutionSummary ignored;
        (void)ps2::ui::ValidateSwfExecutionBytes(bad.data(), bad.size(), ignored);
    }
}

void CheckFile(const char * path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    const Bytes bytes{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    ps2::ui::SwfExecutionSummary summary;
    assert(ps2::ui::ValidateSwfExecutionBytes(bytes.data(), bytes.size(), summary));
    std::printf("SWF execution admission: bytes=%zu actions=%u functions=%u placements=%u removals=%u functionDepth=%u\n",
                bytes.size(), summary.actions, summary.functions, summary.placements, summary.removals, summary.maxFunctionDepth);
}
} // namespace

int main(int argc, char ** argv)
{
    ps2::ui::SwfExecutionSummary summary;
    assert(!ps2::ui::ValidateSwfActions(nullptr, 0, 1, summary));
    assert(!ps2::ui::ValidateSwfExecutionBytes(nullptr, 0, summary));
    Actions();
    Commands();
    Movies();
    CheckFile("assets/ui/base/generated/swf/ps2_demo.bswf");

    for (int argument = 1; argument < argc; ++argument)
    {
        CheckFile(argv[argument]);
    }

    std::printf("SWF execution admission tests passed.\n");
    return 0;
}
