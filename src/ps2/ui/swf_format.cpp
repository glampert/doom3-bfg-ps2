// ================================================================================================
// File: swf_format.cpp
// Brief: Check native BSWF allocation ranges before retaining opaque, inactive timelines and scripts.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/swf_format.h"
#include <cstring>

namespace ps2::ui
{
namespace
{
using U32 = std::uint32_t;
using U8 = std::uint8_t;
static constexpr U32 kMaxDictionary = 1024;
static constexpr U32 kMaxItems = 65536;

class Reader final
{
public:
    Reader(const void * bytes, std::size_t length) : m_data(static_cast<const U8 *>(bytes)), m_left(length)
    {
    }

    U32 Read(unsigned count, bool little = false)
    {
        const U8 * const data = Take(count);

        if (data == nullptr)
        {
            return 0;
        }

        U32 value = 0;
        for (unsigned index = 0; index < count; ++index)
        {
            value = (value << 8) | data[little ? count - index - 1 : index];
        }

        return value;
    }

    const U8 * Take(std::size_t count)
    {
        if (!m_valid || count > m_left)
        {
            m_valid = false;
            return nullptr;
        }

        const U8 * const data = m_data;
        m_data += count;
        m_left -= count;
        return data;
    }

    U32 Count(U32 stride = 1, U32 maximum = kMaxItems)
    {
        const U32 count = Read(4);
        Require(count <= maximum && count <= m_left / stride);
        return m_valid ? count : 0;
    }

    void Floats(U32 count)
    {
        for (U32 index = 0; index < count && m_valid; ++index)
        {
            // Inspect IEEE exponent bits: the EE cannot represent Inf/NaN itself.
            Require((Read(4) & 0x7f800000U) != 0x7f800000U);
        }
    }

    void String(char * output = nullptr, std::size_t capacity = 0)
    {
        const U32 count = Read(4, true);
        Require(count <= 4096 && (output == nullptr || count < capacity));
        const U8 * const data = Take(count);

        if (data == nullptr)
        {
            return;
        }

        for (U32 index = 0; index < count; ++index)
        {
            Require(data[index] != 0);

            if (output != nullptr)
            {
                Require(data[index] >= 32 && data[index] <= 126);
                output[index] = static_cast<char>(data[index]);
            }
        }

        if (output != nullptr)
        {
            output[count] = '\0';
        }
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
        return m_valid && m_left == 0;
    }

    const U8 * Pointer() const
    {
        return m_data;
    }

private:
    const U8 * m_data;
    std::size_t m_left;
    bool m_valid = true;
};

class Validator final
{
public:
    Validator(const void * bytes, std::size_t length, SwfIdentityCheck identity, void * context, SwfExecutionSummary * execution = nullptr) : m_reader(bytes, length), m_identity(identity), m_context(context), m_execution(execution)
    {
    }

    bool Run(SwfSummary & output)
    {
        auto & r = m_reader;
        r.Require(r.Read(4) == 0x42535710U);
        r.Take(8);
        // Positive finite dimensions, at most 16384; rate is 8.8 fixed point.
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            const U32 dimension = r.Read(4);
            r.Require(dimension > 0 && dimension <= 0x46800000U);
        }

        r.Require(r.Read(2) != 0);
        Sprite();
        m_summary.dictionary = r.Count(4, kMaxDictionary);
        r.Require(m_summary.dictionary != 0);

        for (U32 index = 0; index < m_summary.dictionary && r.Valid(); ++index)
        {
            const U32 kind = r.Read(4);
            m_types[index] = static_cast<U8>(kind);

            switch (kind)
            {
            case 0:
                break;
            case 1:
                Image();
                break;
            case 2:
                Shape();
                break;
            case 4:
                Sprite(index);
                break;
            case 5:
                Font();
                break;
            case 7:
                EditText(index);
                break;
            default:
                // The selected shell/dialog contain no morph or static-text records.
                r.Require(false);
                break;
            }
        }

        for (U32 index = 0; index < m_references && r.Valid(); ++index)
        {
            r.Require(m_fontRefs[index] < m_summary.dictionary && m_types[m_fontRefs[index]] == 5);
        }

        if (!r.End() || m_summary.fonts == 0 || m_summary.editTexts == 0 || !m_hasPreview)
        {
            return false;
        }

        if (m_execution != nullptr && !Execution())
        {
            return false;
        }

        output = m_summary;
        return true;
    }

private:
    void Name(bool font)
    {
        char name[256] = {};
        m_reader.String(name, sizeof(name));
        m_reader.Require(name[0] != '\0');

        if (m_reader.Valid() && (font || strcmp(name, ".") != 0))
        {
            m_reader.Require(m_identity == nullptr || m_identity(name, font, m_context));
        }
    }

    U32 Vertices()
    {
        const U32 count = m_reader.Count(8, 16384);
        m_summary.vertices += count;
        m_reader.Require(m_summary.vertices <= 32768);
        m_reader.Floats(count * 2);
        return count;
    }

    void Indices(U32 vertices)
    {
        const U32 count = m_reader.Count(2);
        m_summary.indices += count;
        m_reader.Require(count % 3 == 0 && m_summary.indices <= kMaxItems);

        for (U32 index = 0; index < count && m_reader.Valid(); ++index)
        {
            m_reader.Require(m_reader.Read(2) < vertices);
        }
    }

    void Sprite(U32 dictionary = kMaxDictionary)
    {
        auto & r = m_reader;
        ++m_summary.sprites;
        r.Require(m_summary.sprites <= 512);
        const U32 frames = r.Read(2);
        const U32 offsets = r.Count(4, 8193);
        r.Require(frames != 0 && offsets == frames + 1);
        U32 previous = 0;

        for (U32 index = 0; index < offsets && r.Valid(); ++index)
        {
            const U32 next = r.Read(4);
            r.Require((index != 0 || next == 0) && next >= previous && next <= 32768);
            previous = next;
        }

        const U32 labels = r.Count(8, 8192);
        for (U32 index = 0; index < labels && r.Valid(); ++index)
        {
            const U32 frame = r.Read(4);
            r.Require(frame > 0 && frame <= frames);
            r.String();
        }

        const U32 bufferBytes = r.Read(4);
        const U8 * const buffer = r.Take(bufferBytes);
        const U32 commands = r.Count(8, 32768);
        const U8 * const descriptors = r.Pointer();
        m_summary.commands += commands;
        r.Require(commands == previous && m_summary.commands <= 32768);
        U32 used = 0;

        for (U32 index = 0; index < commands && r.Valid(); ++index)
        {
            const U32 tag = r.Read(4);
            const U32 length = r.Read(4);
            r.Require(tag == 12 || tag == 26 || tag == 28 || tag == 70);
            r.Require(used <= bufferBytes && length <= bufferBytes - used);
            used += length;
        }

        // Native sprite Init executes doInitActions even before its first frame. This storage-only
        // profile rejects them, so constructing the inactive root cannot execute any imported code.
        r.Require(r.Read(4) == 0 && used == bufferBytes);

        if (m_execution != nullptr && r.Valid())
        {
            m_sprites[m_spriteCount++] = { buffer, descriptors, commands, dictionary, frames };
        }
    }

    struct SpriteData
    {
        const U8 * buffer = nullptr;
        const U8 * descriptors = nullptr;
        U32 commands = 0;
        U32 dictionary = kMaxDictionary;
        U32 frames = 0;
    };

    static U32 Big(const U8 * bytes)
    {
        return (static_cast<U32>(bytes[0]) << 24) | (static_cast<U32>(bytes[1]) << 16) |
               (static_cast<U32>(bytes[2]) << 8) | static_cast<U32>(bytes[3]);
    }

    bool Execution()
    {
        SwfExecutionSummary summary;
        std::uint16_t display[kMaxSwfDisplayDepth + 1];

        for (U32 index = 0; index < m_spriteCount; ++index)
        {
            const SpriteData & sprite = m_sprites[index];
            std::memset(display, 0xff, sizeof(display));
            U32 used = 0;

            for (U32 command = 0; command < sprite.commands; ++command)
            {
                const U32 tag = Big(sprite.descriptors + command * 8);
                const U32 length = Big(sprite.descriptors + command * 8 + 4);
                SwfPlacement placement;

                if (!ValidateSwfCommand(tag, sprite.buffer + used, length, m_types,
                                        m_summary.dictionary, sprite.frames, summary, &placement))
                {
                    return false;
                }

                used += length;

                if (tag == 12)
                {
                    continue;
                }

                auto & character = display[placement.depth];

                if (placement.remove)
                {
                    // Native removal of a missing depth is an intentional no-op.
                    character = 0xffffU;
                }
                else if (placement.move)
                {
                    if (character == 0xffffU)
                    {
                        return false;
                    }

                    if (placement.hasCharacter)
                    {
                        const U32 oldKind = m_types[character];
                        const U32 newKind = m_types[placement.character];

                        if (oldKind == 4 || oldKind == 7 || newKind == 4 || newKind == 7)
                        {
                            return false;
                        }

                        character = placement.character;
                    }
                }
                else
                {
                    if (character != 0xffffU)
                    {
                        return false;
                    }

                    character = placement.character;
                }
            }
        }

        for (U32 index = 0; index < m_spriteCount; ++index)
        {
            if (!Graph(index))
            {
                return false;
            }
        }

        *m_execution = summary;
        return true;
    }

    bool Graph(U32 index, U32 depth = 1)
    {
        if (depth > kMaxSwfSpriteDepth || m_graphState[index] == 1)
        {
            return false;
        }

        if (m_graphState[index] == 2)
        {
            return depth + m_graphHeight[index] - 1 <= kMaxSwfSpriteDepth;
        }

        m_graphState[index] = 1;
        U32 height = 1;
        const SpriteData & sprite = m_sprites[index];
        U32 used = 0;

        for (U32 command = 0; command < sprite.commands; ++command)
        {
            const U32 tag = Big(sprite.descriptors + command * 8);
            const U32 length = Big(sprite.descriptors + command * 8 + 4);
            const U8 * const data = sprite.buffer + used;
            used += length;

            if ((tag != 26 && tag != 70) || (data[0] & 2U) == 0)
            {
                continue;
            }

            const U32 characterOffset = tag == 26 ? 3 : 4;
            const U32 character = static_cast<U32>(data[characterOffset]) |
                                  (static_cast<U32>(data[characterOffset + 1]) << 8);

            if (m_types[character] != 4)
            {
                continue;
            }

            U32 child = 0;

            while (child < m_spriteCount && m_sprites[child].dictionary != character)
            {
                ++child;
            }

            if (child == m_spriteCount || !Graph(child, depth + 1))
            {
                return false;
            }

            const U32 childHeight = static_cast<U32>(m_graphHeight[child]) + 1;

            if (childHeight > height)
            {
                height = childHeight;
            }
        }

        m_graphHeight[index] = static_cast<U8>(height);
        m_graphState[index] = 2;
        return true;
    }

    void Image()
    {
        Name(false);
        for (unsigned axis = 0; axis < 2; ++axis)
        {
            const U32 size = m_reader.Read(4);
            const U32 offset = m_reader.Read(4);
            m_reader.Require(size > 0 && size <= 16384 && offset <= 16384 && size + offset <= 16384);
        }

        m_reader.Floats(4);
    }

    void Shape()
    {
        auto & r = m_reader;
        r.Floats(8);
        const U32 fills = r.Count(73, 1024);

        for (U32 index = 0; index < fills && r.Valid(); ++index)
        {
            const U32 kind = r.Read(1);
            const U32 subtype = r.Read(1);
            r.Require(kind == 0 || (kind == 4 && subtype <= 3));
            r.Take(8);
            r.Floats(12);
            r.Require(r.Read(1) == 0);
            r.Floats(1);
            r.Read(2);
            const U32 start = Vertices();
            const U32 end = Vertices();
            r.Require(end == 0 || end == start);
            Indices(start);
        }

        const U32 lines = r.Count(24, 1024);
        for (U32 index = 0; index < lines && r.Valid(); ++index)
        {
            r.Take(12);
            const U32 start = Vertices();
            const U32 end = Vertices();
            r.Require(end == 0 || end == start);
            Indices(start);
        }
    }

    void Font()
    {
        Name(true);
        ++m_summary.fonts;
        m_reader.Require(m_summary.fonts <= 8);
        m_reader.Take(6);
        const U32 glyphs = m_reader.Count(12, 2048);

        for (U32 index = 0; index < glyphs && m_reader.Valid(); ++index)
        {
            m_reader.Take(4);
            Indices(Vertices());
        }
    }

    void EditText(U32 index)
    {
        auto & r = m_reader;
        float bounds[4] = {};
        for (unsigned axis = 0; axis < 4; ++axis)
        {
            const U32 bits = r.Read(4);
            r.Require((bits & 0x7fffffffU) <= 0x46800000U);

            if (r.Valid())
            {
                std::memcpy(&bounds[axis], &bits, sizeof(bits));
            }
        }

        r.Require(bounds[2] > bounds[0] && bounds[3] > bounds[1]);
        r.Require((r.Read(4) & ~63U) == 0);
        const U32 font = r.Read(2);
        const U32 height = r.Read(2);
        r.Require(m_references < 256);

        if (m_references < 256)
        {
            m_fontRefs[m_references++] = font;
        }

        if (!m_hasPreview && height >= 400)
        {
            m_summary.previewText = index;
            m_hasPreview = true;
        }

        r.Require(height != 0 && height <= 4096);
        r.Take(6);
        r.Require(r.Read(4) <= 3);
        r.Take(8);
        r.String();
        r.String();
        ++m_summary.editTexts;
    }

    Reader m_reader;
    SwfIdentityCheck m_identity;
    void * m_context;
    SwfSummary m_summary;
    U8 m_types[kMaxDictionary] = {};
    U32 m_fontRefs[256] = {};
    U32 m_references = 0;
    bool m_hasPreview = false;
    SwfExecutionSummary * m_execution;
    SpriteData m_sprites[512];
    U32 m_spriteCount = 0;
    U8 m_graphState[512] = {};
    U8 m_graphHeight[512] = {};
};
} // namespace

bool ValidateSwfBytes(const void * bytes, std::size_t length, SwfSummary & output, SwfIdentityCheck identity, void * context)
{
    if (bytes == nullptr || length < 64 || length > kMaxSwfBytes)
    {
        return false;
    }

    return Validator(bytes, length, identity, context).Run(output);
}

bool ValidateSwfExecutionBytes(const void * bytes, std::size_t length, SwfExecutionSummary & output)
{
    if (bytes == nullptr || length < 64 || length > kMaxSwfBytes)
    {
        return false;
    }

    SwfSummary storage;
    SwfExecutionSummary execution;

    if (!Validator(bytes, length, nullptr, nullptr, &execution).Run(storage))
    {
        return false;
    }

    output = execution;
    return true;
}

bool ValidateEnglishBytes(const void * bytes, std::size_t length, std::uint32_t & entries)
{
    if (bytes == nullptr || length == 0 || length > 1024U * 1024U)
    {
        return false;
    }

    const auto * const data = static_cast<const U8 *>(bytes);
    std::size_t cursor = length >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf ? 3 : 0;
    U32 count = 0;
    const auto skip = [&]()
    {
        while (cursor < length)
        {
            const U8 c = data[cursor];

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++cursor;
            }
            else if (c == '/' && cursor + 1 < length && data[cursor + 1] == '/')
            {
                while (cursor < length && data[cursor] != '\n')
                {
                    ++cursor;
                }
            }
            else
            {
                break;
            }
        }
    };
    skip();

    if (cursor == length || data[cursor++] != '{')
    {
        return false;
    }

    for (;;)
    {
        skip();

        if (cursor == length)
        {
            return false;
        }

        if (data[cursor] == '}')
        {
            ++cursor;
            skip();

            if (cursor != length || count == 0)
            {
                return false;
            }

            entries = count;
            return true;
        }

        if (data[cursor++] != '"' || ++count > 16384)
        {
            return false;
        }

        const std::size_t start = cursor;

        while (cursor < length && data[cursor] != '"')
        {
            const U8 c = data[cursor++];

            if (c < 33 || c > 126 || c == '\\' || cursor - start > 512)
            {
                return false;
            }
        }

        if (cursor == length || cursor == start)
        {
            return false;
        }

        ++cursor;

        while (cursor < length && (data[cursor] == ' ' || data[cursor] == '\t'))
        {
            ++cursor;
        }

        if (cursor == length || data[cursor++] != '"')
        {
            return false;
        }

        std::size_t valueBytes = 0;
        bool closed = false;

        while (cursor < length && ++valueBytes <= 16384)
        {
            const U8 c = data[cursor++];

            if (c == '"')
            {
                closed = true;
                break;
            }

            if (c == 0 || c == '\n' || c == '\r')
            {
                return false;
            }

            if (c == '\\')
            {
                if (cursor == length)
                {
                    return false;
                }

                const U8 escaped = data[cursor++];

                if (escaped != 'n' && escaped != 't' && escaped != '"' && escaped != '\\')
                {
                    return false;
                }
            }
        }

        if (!closed)
        {
            return false;
        }

        // Native Load keeps reading a value until LF; require that exact boundary. Whitespace or
        // another pair after the closing quote would otherwise become part of the native value.
        if (cursor < length && data[cursor] == '\r')
        {
            ++cursor;
        }

        if (cursor == length || data[cursor++] != '\n')
        {
            return false;
        }
    }
}
} // namespace ps2::ui
