// ================================================================================================
// File: swf_execution.cpp
// Brief: Bounded native placement and AVM1 operand/branch/function admission without execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/swf_execution.h"
#include <cstring>

namespace ps2::ui
{
namespace
{
using U8 = std::uint8_t;
using U32 = std::uint32_t;
static constexpr U32 kMaxStringBytes = 4096;
static constexpr U32 kMaxConstants = 1024;
static constexpr U32 kMaxParameters = 16;

class Reader final
{
public:
    Reader(const void * bytes, std::size_t length) : m_data(static_cast<const U8 *>(bytes)), m_length(length)
    {
    }

    U32 Read(unsigned count = 1)
    {
        const U8 * const data = Take(count);
        U32 value = 0;

        if (data != nullptr)
        {
            for (unsigned index = 0; index < count; ++index)
            {
                value |= static_cast<U32>(data[index]) << (index * 8);
            }
        }

        return value;
    }

    const U8 * Take(std::size_t length)
    {
        if (!m_valid || length > Left())
        {
            m_valid = false;
            return nullptr;
        }

        const U8 * const data = m_data + m_cursor;
        m_cursor += length;
        return data;
    }

    bool String()
    {
        U32 size = 0;

        while (Valid() && Left() != 0 && size <= kMaxStringBytes)
        {
            const U32 value = Read();

            if (value == 0)
            {
                return true;
            }

            Require(value >= 32 || value == 9 || value == 10 || value == 13);
            ++size;
        }

        Require(false);
        return false;
    }

    void Require(bool condition)
    {
        m_valid = m_valid && condition;
    }

    bool Valid() const
    {
        return m_valid;
    }

    bool End() const
    {
        return m_valid && m_cursor == m_length;
    }

    std::size_t Tell() const
    {
        return m_cursor;
    }

    std::size_t Left() const
    {
        return m_length - m_cursor;
    }

private:
    const U8 * m_data;
    std::size_t m_length;
    std::size_t m_cursor = 0;
    bool m_valid = true;
};

class Bits final
{
public:
    explicit Bits(Reader & reader) : m_reader(reader)
    {
    }

    U32 Read(unsigned width)
    {
        U32 value = 0;

        for (unsigned index = 0; index < width; ++index)
        {
            if (m_left == 0)
            {
                m_value = m_reader.Read();
                m_left = 8;
            }

            --m_left;
            value = (value << 1) | ((m_value >> m_left) & 1U);
        }

        return value;
    }

private:
    Reader & m_reader;
    U32 m_value = 0;
    unsigned m_left = 0;
};

void Matrix(Reader & reader)
{
    Bits bits(reader);

    for (unsigned pair = 0; pair < 2; ++pair)
    {
        if (bits.Read(1) != 0)
        {
            const U32 width = bits.Read(5);
            reader.Require(width != 0);
            bits.Read(width);
            bits.Read(width);
        }
    }

    const U32 width = bits.Read(5);
    bits.Read(width);
    bits.Read(width);
}

void Color(Reader & reader)
{
    Bits bits(reader);
    const U32 add = bits.Read(1);
    const U32 multiply = bits.Read(1);
    const U32 width = bits.Read(4);
    reader.Require((add == 0 && multiply == 0) || width != 0);

    for (U32 component = 0; component < (add + multiply) * 4; ++component)
    {
        bits.Read(width);
    }
}

bool PlainAction(U32 opcode)
{
    // Exact action-family union observed in the authored demo and selected retail shell/dialog.
    // Admitting another interpreter API requires its own semantic/runtime review first.
    switch (opcode)
    {
    case 0x00: // End.
    case 0x06: // Play.
    case 0x07: // Stop.
    case 0x0a: // Add.
    case 0x0b: // Subtract.
    case 0x0c: // Multiply.
    case 0x12: // Not.
    case 0x17: // Pop.
    case 0x1c: // GetVariable.
    case 0x1d: // SetVariable.
    case 0x26: // Trace.
    case 0x3c: // DefineLocal.
    case 0x3d: // CallFunction.
    case 0x47: // Add2.
    case 0x48: // Less2.
    case 0x49: // Equals2.
    case 0x4b: // ToString.
    case 0x4c: // PushDuplicate.
    case 0x4e: // GetMember.
    case 0x4f: // SetMember.
    case 0x50: // Increment.
    case 0x51: // Decrement.
    case 0x52: // CallMethod.
    case 0x67: // Greater.
        return true;
    default:
        return false;
    }
}

class ActionValidator final
{
public:
    ActionValidator(U32 frames, SwfExecutionSummary & summary) : m_frames(frames), m_summary(summary)
    {
    }

    bool Run(const void * bytes, std::size_t length, U32 depth = 0, U32 registers = 4, U32 constants = 0)
    {
        if (length > kMaxSwfActionBytes || depth > kMaxSwfFunctionDepth || ++m_summary.actionBlocks > kMaxSwfActions)
        {
            return false;
        }

        if (depth > m_summary.maxFunctionDepth)
        {
            m_summary.maxFunctionDepth = depth;
        }

        Reader stream(bytes, length);

        while (stream.Valid() && stream.Left() != 0)
        {
            const std::size_t start = stream.Tell();
            const U32 opcode = stream.Read();
            const U32 size = opcode >= 0x80 ? stream.Read(2) : 0;
            Reader body(stream.Take(size), size);
            stream.Require(body.Valid() && ++m_summary.actions <= kMaxSwfActions);

            if (!stream.Valid())
            {
                return false;
            }

            if (opcode < 0x80)
            {
                body.Require(PlainAction(opcode));
            }
            else
            {
                switch (opcode)
                {
                case 0x81: // GotoFrame uses zero-based stored frame numbers.
                    body.Require(body.Read(2) < m_frames);
                    break;
                case 0x87:
                    body.Require(body.Read() < registers);
                    break;
                case 0x88:
                    // A unique entry pool makes all branch paths inherit the same constant table.
                    body.Require(start == 0);
                    constants = body.Read(2);
                    body.Require(constants <= kMaxConstants);

                    for (U32 index = 0; index < constants && body.Valid(); ++index)
                    {
                        body.String();
                    }

                    break;
                case 0x8e: // DefineFunction2.
                case 0x9b: // DefineFunction.
                {
                    body.String();
                    const U32 parameters = body.Read(2);
                    U32 childRegisters = 4;

                    if (opcode == 0x8e)
                    {
                        const U32 storedRegisters = body.Read();
                        // The native provider retains the documented 256-register encoding.
                        childRegisters = storedRegisters + 1;
                        const U32 flags = body.Read(2);
                        body.Require((flags & ~0x1ffU) == 0);
                        const U32 preloadBits = flags & 0x1d5U;
                        U32 preloads = 1;

                        for (U32 bit = 0; bit < 9; ++bit)
                        {
                            preloads += (preloadBits >> bit) & 1U;
                        }

                        body.Require(preloads <= childRegisters);
                    }

                    body.Require(parameters <= kMaxParameters && ++m_summary.functions <= kMaxSwfFunctions);

                    for (U32 index = 0; index < parameters && body.Valid(); ++index)
                    {
                        if (opcode == 0x8e)
                        {
                            body.Require(body.Read() < childRegisters);
                        }

                        body.String();
                    }

                    const U32 codeSize = body.Read(2);
                    const U8 * const code = body.Left() == 0 ? stream.Take(codeSize) : body.Take(codeSize);
                    body.Require(code != nullptr && body.Valid() && stream.Valid());

                    if (body.Valid())
                    {
                        body.Require(Run(code, codeSize, depth + 1, childRegisters, constants));
                    }

                    break;
                }
                case 0x96:
                    Push(body, registers, constants);
                    break;
                case 0x99: // Jump.
                case 0x9d: // If.
                    body.Read(2);
                    break;
                default:
                    body.Require(false);
                    break;
                }
            }

            if (!body.End() || !stream.Valid())
            {
                return false;
            }

            if (opcode == 0)
            {
                // Native End immediately returns; reject hidden actions and noncanonical padding.
                if (!stream.End())
                {
                    return false;
                }
            }
        }

        return stream.End() && Branches(bytes, length);
    }

private:
    void Push(Reader & body, U32 registers, U32 constants)
    {
        body.Require(body.Left() != 0);

        while (body.Valid() && body.Left() != 0)
        {
            switch (body.Read())
            {
            case 0:
                body.String();
                break;
            case 1:
                body.Require((body.Read(4) & 0x7f800000U) != 0x7f800000U);
                break;
            case 2:
            case 3:
                break;
            case 4:
                body.Require(body.Read() < registers);
                break;
            case 5:
                body.Require(body.Read() <= 1);
                break;
            case 6:
            {
                // SWF stores double words swapped. The native interpreter converts to EE float.
                const std::uint64_t high = body.Read(4);
                const std::uint64_t low = body.Read(4);
                const std::uint64_t bits = ((high << 32) | low) & 0x7fffffffffffffffULL;
                body.Require(bits <= 0x47efffffe0000000ULL);
                break;
            }
            case 7:
                body.Read(4);
                break;
            case 8:
                body.Require(body.Read() < constants);
                break;
            case 9:
                body.Require(body.Read(2) < constants);
                break;
            default:
                body.Require(false);
                break;
            }
        }
    }

    // The first pass validated operands and child blocks. Rescan only the enclosing action
    // boundaries so one fixed bitset serves all nesting levels, without a large recursive stack.
    U32 Skip(Reader & stream)
    {
        const U32 opcode = stream.Read();
        const U32 size = opcode >= 0x80 ? stream.Read(2) : 0;
        Reader body(stream.Take(size), size);

        if (opcode == 0x8e || opcode == 0x9b)
        {
            body.String();
            const U32 parameters = body.Read(2);

            if (opcode == 0x8e)
            {
                body.Read(3);
            }

            for (U32 index = 0; index < parameters; ++index)
            {
                if (opcode == 0x8e)
                {
                    body.Read();
                }

                body.String();
            }

            const U32 codeSize = body.Read(2);

            if (body.Left() == 0)
            {
                stream.Take(codeSize);
            }
        }
        return opcode;
    }

    bool Branches(const void * bytes, std::size_t length)
    {
        std::memset(m_boundaries, 0, sizeof(m_boundaries));
        Reader boundaries(bytes, length);

        while (boundaries.Left() != 0)
        {
            Mark(boundaries.Tell());
            Skip(boundaries);
        }

        Mark(length);
        Reader stream(bytes, length);

        while (stream.Left() != 0)
        {
            const std::size_t start = stream.Tell();
            const U32 opcode = Skip(stream);

            if (opcode == 0x99 || opcode == 0x9d)
            {
                const auto * const data = static_cast<const U8 *>(bytes);
                const U32 encoded = static_cast<U32>(data[start + 3]) | (static_cast<U32>(data[start + 4]) << 8);
                const std::int32_t delta = encoded >= 0x8000U ? static_cast<std::int32_t>(encoded) - 65536 : static_cast<std::int32_t>(encoded);
                const auto target = static_cast<std::int32_t>(stream.Tell()) + delta;

                if (target < 0 || static_cast<std::size_t>(target) > length ||
                    (m_boundaries[static_cast<U32>(target) / 8] & (1U << (static_cast<U32>(target) % 8))) == 0)
                {
                    return false;
                }
            }
        }

        return true;
    }

    void Mark(std::size_t position)
    {
        m_boundaries[position / 8] |= static_cast<U8>(1U << (position % 8));
    }

    U32 m_frames;
    SwfExecutionSummary & m_summary;
    U8 m_boundaries[(kMaxSwfActionBytes + 1) / 8] = {};
};
} // namespace

bool ValidateSwfActions(const void * bytes, std::size_t length, U32 frames, SwfExecutionSummary & output)
{
    if (bytes == nullptr || frames == 0 || length > kMaxSwfActionBytes)
    {
        return false;
    }

    SwfExecutionSummary summary = output;
    ActionValidator validator(frames, summary);

    if (!validator.Run(bytes, length))
    {
        return false;
    }

    output = summary;
    return true;
}

bool ValidateSwfCommand(U32 tag, const void * bytes, std::size_t length, const U8 * dictionaryTypes,
                        U32 dictionaryCount, U32 frames, SwfExecutionSummary & output, SwfPlacement * placement)
{
    if (bytes == nullptr || dictionaryTypes == nullptr || dictionaryCount == 0)
    {
        return false;
    }

    SwfExecutionSummary summary = output;
    SwfPlacement result;
    Reader reader(bytes, length);

    if (tag == 12)
    {
        if (!ValidateSwfActions(bytes, length, frames, summary))
        {
            return false;
        }

        reader.Take(length);
    }
    else if (tag == 28)
    {
        result.remove = true;
        result.depth = static_cast<std::uint16_t>(reader.Read(2));
        ++summary.removals;
    }
    else if (tag == 26 || tag == 70)
    {
        const U32 flags = reader.Read();
        const U32 flags2 = tag == 70 ? reader.Read() : 0;
        // Native ignores these features, or stops decoding the command before later fields.
        reader.Require((flags & 0x80U) == 0 && (flags2 & ~2U) == 0);
        result.depth = static_cast<std::uint16_t>(reader.Read(2));
        result.move = (flags & 1U) != 0;
        result.hasCharacter = (flags & 2U) != 0;
        reader.Require(result.move || result.hasCharacter);

        if (result.hasCharacter)
        {
            result.character = static_cast<std::uint16_t>(reader.Read(2));
            reader.Require(result.character < dictionaryCount);

            if (reader.Valid())
            {
                const U32 kind = dictionaryTypes[result.character];
                reader.Require(kind == 1 || kind == 2 || kind == 4 || kind == 7);
            }
        }

        if ((flags & 4U) != 0)
        {
            Matrix(reader);
        }

        if ((flags & 8U) != 0)
        {
            Color(reader);
        }

        if ((flags & 16U) != 0)
        {
            reader.Read(2);
        }

        if ((flags & 32U) != 0)
        {
            reader.String();
        }

        if ((flags & 64U) != 0)
        {
            result.clipDepth = static_cast<std::uint16_t>(reader.Read(2));
            reader.Require(result.clipDepth > result.depth && result.clipDepth <= kMaxSwfDisplayDepth);
        }

        if ((flags2 & 2U) != 0)
        {
            const U32 blend = reader.Read();
            // These retail modes are structurally admitted; drawing stays separately gated.
            reader.Require(blend == 0 || blend == 1 || blend == 4 || blend == 8);
        }

        ++summary.placements;
    }
    else
    {
        return false;
    }

    if (!reader.End() || (tag != 12 && (result.depth == 0 || result.depth > kMaxSwfDisplayDepth)))
    {
        return false;
    }

    output = summary;

    if (placement != nullptr)
    {
        *placement = result;
    }

    return true;
}
} // namespace ps2::ui
