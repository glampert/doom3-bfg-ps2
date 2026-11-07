// ================================================================================================
// File: proc.cpp
// Brief: Parsers of dmap's text .proc files and of id's binary .bproc cache for the world cook.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/proc.h"

#include "assets/bytes.h"

#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace assets
{
namespace
{

static constexpr std::size_t kMaxSurfaceVertices = 65535;
static constexpr long kMaxCount = 16 * 1024 * 1024;

// What both readers require of the parsed file: every area has its model, the nodes exist and
// their children stay in range.
static bool CheckTopology(ProcFile & proc, std::string & error)
{
    // A map without portals is one area, as idRenderWorldLocal::ClearWorld makes it.
    if (proc.portals.empty() && proc.areaCount == 0)
    {
        proc.areaCount = 1;
    }

    for (int area = 0; area < proc.areaCount; ++area)
    {
        if (FindAreaModel(proc, area) == nullptr)
        {
            error = "missing model _area" + std::to_string(area);
            return false;
        }
    }

    if (proc.nodes.empty())
    {
        error = "no area nodes";
        return false;
    }

    for (const ProcNode & node : proc.nodes)
    {
        for (const int child : node.children)
        {
            if ((child > 0 && static_cast<std::size_t>(child) >= proc.nodes.size()) || (child < 0 && -1 - child >= proc.areaCount))
            {
                error = "node child out of range";
                return false;
            }
        }
    }

    return true;
}

// idLexer's subset that .proc files use: // and /* */ comments, quoted strings, the single
// punctuation tokens { } ( ) and bare words (numbers and names).
class Tokens final
{
public:
    explicit Tokens(std::string_view text)
        : m_text(text)
    {
    }

    bool Next(std::string_view & token)
    {
        SkipSpace();

        if (m_offset >= m_text.size())
        {
            return false;
        }

        const std::size_t start = m_offset;
        const char first = m_text[m_offset];

        if (first == '"')
        {
            const std::size_t end = m_text.find('"', start + 1);

            if (end == std::string_view::npos)
            {
                m_offset = m_text.size();
                token = {};
                m_unterminated = true;
                return false;
            }

            m_offset = end + 1;
            token = m_text.substr(start, m_offset - start);
            return true;
        }

        if (IsPunctuation(first))
        {
            ++m_offset;
            token = m_text.substr(start, 1);
            return true;
        }

        while (m_offset < m_text.size() && !IsSpace(m_text[m_offset]) && !IsPunctuation(m_text[m_offset]) && m_text[m_offset] != '"')
        {
            ++m_offset;
        }

        token = m_text.substr(start, m_offset - start);
        return true;
    }

    bool Unterminated() const
    {
        return m_unterminated;
    }

    int Line() const
    {
        int line = 1;

        for (std::size_t index = 0; index < m_offset && index < m_text.size(); ++index)
        {
            line += m_text[index] == '\n' ? 1 : 0;
        }

        return line;
    }

private:
    static bool IsSpace(char character)
    {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    }

    static bool IsPunctuation(char character)
    {
        return character == '{' || character == '}' || character == '(' || character == ')';
    }

    void SkipSpace()
    {
        while (m_offset < m_text.size())
        {
            if (IsSpace(m_text[m_offset]))
            {
                ++m_offset;
            }
            else if (m_text.substr(m_offset, 2) == "//")
            {
                const std::size_t end = m_text.find('\n', m_offset);
                m_offset = end == std::string_view::npos ? m_text.size() : end + 1;
            }
            else if (m_text.substr(m_offset, 2) == "/*")
            {
                const std::size_t end = m_text.find("*/", m_offset + 2);
                m_offset = end == std::string_view::npos ? m_text.size() : end + 2;
            }
            else
            {
                return;
            }
        }
    }

    std::string_view m_text;
    std::size_t m_offset = 0;
    bool m_unterminated = false;
};

class Parser final
{
public:
    Parser(std::string_view text, std::string & error)
        : m_tokens(text)
        , m_error(error)
    {
    }

    bool Fail(const std::string & message)
    {
        if (m_error.empty())
        {
            m_error = "proc line " + std::to_string(m_tokens.Line()) + ": " + message;
        }

        return false;
    }

    bool Word(std::string_view & token)
    {
        if (!m_tokens.Next(token))
        {
            return Fail(m_tokens.Unterminated() ? "unterminated string" : "unexpected end of file");
        }

        return true;
    }

    bool Expect(std::string_view expected)
    {
        std::string_view token;
        return Word(token) && (token == expected || Fail("expected '" + std::string(expected) + "', found '" + std::string(token) + "'"));
    }

    bool Name(std::string & output)
    {
        std::string_view token;

        if (!Word(token))
        {
            return false;
        }

        if (token.size() >= 2 && token.front() == '"')
        {
            token = token.substr(1, token.size() - 2);
        }

        if (token.empty() || token.size() > 255)
        {
            return Fail("invalid name");
        }

        output = std::string(token);
        return true;
    }

    bool Integer(long minimum, long maximum, long & output)
    {
        std::string_view token;

        if (!Word(token))
        {
            return false;
        }

        const std::string text(token);
        char * end = nullptr;
        errno = 0;
        const long value = std::strtol(text.c_str(), &end, 10);

        if (text.empty() || end != text.c_str() + text.size() || errno != 0 || value < minimum || value > maximum)
        {
            return Fail("invalid integer '" + text + "'");
        }

        output = value;
        return true;
    }

    bool Count(std::size_t & output)
    {
        long value = 0;

        if (!Integer(0, kMaxCount, value))
        {
            return false;
        }

        output = static_cast<std::size_t>(value);
        return true;
    }

    bool Float(float & output)
    {
        std::string_view token;

        if (!Word(token))
        {
            return false;
        }

        const std::string text(token);
        char * end = nullptr;
        errno = 0;
        const float value = std::strtof(text.c_str(), &end);

        if (text.empty() || end != text.c_str() + text.size() || errno != 0 || !std::isfinite(value))
        {
            return Fail("invalid number '" + text + "'");
        }

        output = value;
        return true;
    }

    template<std::size_t N>
    bool Matrix(float (&values)[N])
    {
        if (!Expect("("))
        {
            return false;
        }

        for (float & value : values)
        {
            if (!Float(value))
            {
                return false;
            }
        }

        return Expect(")");
    }

    bool Model(ProcModel & model)
    {
        std::size_t surfaceCount = 0;

        if (!Expect("{") || !Name(model.name) || !Count(surfaceCount))
        {
            return false;
        }

        model.surfaces.resize(surfaceCount);

        for (ProcSurface & surface : model.surfaces)
        {
            std::size_t vertexCount = 0;
            std::size_t indexCount = 0;

            if (!Expect("{") || !Name(surface.material) || !Count(vertexCount) || !Count(indexCount))
            {
                return false;
            }

            if (vertexCount > kMaxSurfaceVertices || indexCount % 3 != 0)
            {
                return Fail("surface counts out of range in " + model.name);
            }

            surface.vertices.resize(vertexCount);

            for (ProcVertex & vertex : surface.vertices)
            {
                float record[8] = {};

                if (!Matrix(record))
                {
                    return false;
                }

                vertex.position = { record[0], record[1], record[2] };
                vertex.uv = { record[3], record[4] };
                vertex.normal = { record[5], record[6], record[7] };
            }

            surface.indices.resize(indexCount);

            for (std::uint32_t & index : surface.indices)
            {
                long value = 0;

                if (!Integer(0, static_cast<long>(vertexCount) - 1, value))
                {
                    return false;
                }

                index = static_cast<std::uint32_t>(value);
            }

            if (!Expect("}"))
            {
                return false;
            }
        }

        return Expect("}");
    }

    bool ShadowModel()
    {
        std::string name;
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        long ignored = 0;

        if (!Expect("{") || !Name(name) || !Count(vertexCount) || !Integer(0, kMaxCount, ignored) ||
            !Integer(0, kMaxCount, ignored) || !Count(indexCount) || !Integer(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), ignored))
        {
            return false;
        }

        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
        {
            float position[3] = {};

            if (!Matrix(position))
            {
                return false;
            }
        }

        for (std::size_t index = 0; index < indexCount; ++index)
        {
            if (!Integer(0, static_cast<long>(vertexCount) - 1, ignored))
            {
                return false;
            }
        }

        return Expect("}");
    }

    bool Portals(ProcFile & proc)
    {
        std::size_t areaCount = 0;
        std::size_t portalCount = 0;

        if (!Expect("{") || !Count(areaCount) || !Count(portalCount))
        {
            return false;
        }

        if (areaCount > 65535)
        {
            return Fail("too many areas");
        }

        proc.areaCount = static_cast<int>(areaCount);
        proc.portals.resize(portalCount);

        for (ProcPortal & portal : proc.portals)
        {
            std::size_t pointCount = 0;
            long areas[2] = {};

            if (!Count(pointCount) || !Integer(0, static_cast<long>(areaCount) - 1, areas[0]) || !Integer(0, static_cast<long>(areaCount) - 1, areas[1]))
            {
                return false;
            }

            if (pointCount < 3 || pointCount > 64 || areas[0] == areas[1])
            {
                return Fail("invalid portal");
            }

            portal.areas[0] = static_cast<int>(areas[0]);
            portal.areas[1] = static_cast<int>(areas[1]);
            portal.points.resize(pointCount);

            for (std::array<float, 3> & point : portal.points)
            {
                float values[3] = {};

                if (!Matrix(values))
                {
                    return false;
                }

                point = { values[0], values[1], values[2] };
            }
        }

        return Expect("}");
    }

    bool Nodes(ProcFile & proc)
    {
        std::size_t nodeCount = 0;

        if (!Expect("{") || !Count(nodeCount))
        {
            return false;
        }

        proc.nodes.resize(nodeCount);

        for (ProcNode & node : proc.nodes)
        {
            float plane[4] = {};
            long children[2] = {};

            if (!Matrix(plane) || !Integer(-kMaxCount, kMaxCount, children[0]) || !Integer(-kMaxCount, kMaxCount, children[1]))
            {
                return false;
            }

            node.plane = { plane[0], plane[1], plane[2], plane[3] };
            node.children[0] = static_cast<int>(children[0]);
            node.children[1] = static_cast<int>(children[1]);
        }

        return Expect("}");
    }

    bool File(ProcFile & proc)
    {
        std::string_view token;

        if (!m_tokens.Next(token) || token != "mapProcFile003")
        {
            return Fail("not a mapProcFile003 file");
        }

        bool portals = false;
        bool nodes = false;

        while (m_tokens.Next(token))
        {
            if (token == "model")
            {
                proc.models.emplace_back();

                if (!Model(proc.models.back()))
                {
                    return false;
                }
            }
            else if (token == "shadowModel")
            {
                if (!ShadowModel())
                {
                    return false;
                }

                ++proc.shadowModelCount;
            }
            else if (token == "interAreaPortals" && !portals)
            {
                portals = true;

                if (!Portals(proc))
                {
                    return false;
                }
            }
            else if (token == "nodes" && !nodes)
            {
                nodes = true;

                if (!Nodes(proc))
                {
                    return false;
                }
            }
            else
            {
                return Fail("unexpected '" + std::string(token) + "'");
            }
        }

        if (m_tokens.Unterminated())
        {
            return Fail("unterminated string");
        }

        return CheckTopology(proc, m_error);
    }

private:
    Tokens m_tokens;
    std::string & m_error;
};

// ------------------------------------------------------------------------------------------------
// Binary .bproc
// ------------------------------------------------------------------------------------------------

// idRenderWorldLocal::InitFromMap's binary cache (BPROC_VERSION 1) and idRenderModelStatic's binary
// model (BRM_VERSION 108). id's writers mix byte orders: WriteBig values (counts, flags, indexes,
// half texture coordinates, portal points, node planes) are big-endian, while strings' lengths,
// WriteVec3/WriteVec4/WriteFloat values and idDrawVert positions are little-endian.
static constexpr std::uint32_t kBprocMagic = 0x50524F01U; // 'P' 'R' 'O' 1
static constexpr std::uint32_t kBrmMagic = 0x42524D6CU;   // 'B' 'R' 'M' 108
static constexpr std::size_t kDrawVertBytes = 32;         // xyz, st halves, normal, tangent, color, color2
static constexpr std::size_t kMaxNameBytes = 255;
static constexpr std::uint32_t kMaxBinaryCount = 16U * 1024U * 1024U;

// id's F16toF32: a normal half rebiases into a float, anything else is m * 2^-24 with its sign.
static float HalfToFloat(std::uint16_t half)
{
    const std::uint32_t exponent = (half >> 10) & 0x1FU;
    const std::uint32_t mantissa = half & 0x3FFU;
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000U) << 16;

    if (exponent > 0 && exponent < 31)
    {
        return std::bit_cast<float>(sign | ((exponent + 112U) << 23) | (mantissa << 13));
    }

    const float value = static_cast<float>(mantissa) * (1.0f / 16777216.0f);
    return sign != 0 ? -value : value;
}

// idDrawVert's normal bytes (VERTEX_BYTE_TO_FLOAT).
static float ByteToUnit(std::uint8_t value)
{
    return static_cast<float>(value) * (2.0f / 255.0f) - 1.0f;
}

class BinaryParser final
{
public:
    BinaryParser(std::span<const std::uint8_t> data, std::string & error)
        : m_data(data), m_error(error)
    {
    }

    bool File(ProcFile & proc)
    {
        std::uint32_t magic = 0;
        std::uint32_t entryCount = 0;
        std::string mapName;

        if (!Be32(magic) || magic != kBprocMagic)
        {
            return Fail("not a version 1 .bproc file");
        }

        // The map name and its timestamp only tie the cache to its source; the cook ignores both.
        if (!Be32(entryCount) || entryCount > kMaxBinaryCount || !String(mapName, false) || !Skip(8))
        {
            return false;
        }

        bool portals = false;
        bool nodes = false;

        for (std::uint32_t entry = 0; entry < entryCount; ++entry)
        {
            std::string type;

            if (!String(type, false))
            {
                return false;
            }

            for (char & c : type)
            {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }

            // Area and prelight shadow models are both written as "shadowmodel"; the name tells them apart.
            if (type == "model" || type == "shadowmodel")
            {
                ProcModel model;
                bool shadow = false;

                if (!Model(model, shadow))
                {
                    return false;
                }

                if (shadow)
                {
                    ++proc.shadowModelCount;
                }
                else
                {
                    proc.models.push_back(std::move(model));
                }
            }
            else if (type == "interareaportals" && !portals)
            {
                portals = true;

                if (!Portals(proc))
                {
                    return false;
                }
            }
            else if (type == "nodes" && !nodes)
            {
                nodes = true;

                if (!Nodes(proc))
                {
                    return false;
                }
            }
            else
            {
                return Fail("unexpected entry '" + type + "'");
            }
        }

        if (m_cursor != m_data.size())
        {
            return Fail(std::to_string(m_data.size() - m_cursor) + " bytes after the last entry");
        }

        if (!CheckTopology(proc, m_error))
        {
            m_error = "bproc: " + m_error;
            return false;
        }

        return true;
    }

private:
    bool Fail(const std::string & message)
    {
        if (m_error.empty())
        {
            m_error = "bproc offset " + std::to_string(m_cursor) + ": " + message;
        }

        return false;
    }

    bool Take(std::size_t bytes, const std::uint8_t *& output)
    {
        if (bytes > m_data.size() - m_cursor)
        {
            return Fail("unexpected end of file");
        }

        output = m_data.data() + m_cursor;
        m_cursor += bytes;
        return true;
    }

    bool Skip(std::size_t bytes)
    {
        const std::uint8_t * ignored = nullptr;
        return Take(bytes, ignored);
    }

    // Skips 'count' records of 'bytes' each, refusing counts the remaining file cannot hold.
    bool SkipArray(std::uint32_t count, std::size_t bytes)
    {
        if (count > (m_data.size() - m_cursor) / bytes)
        {
            return Fail("array runs past the end of the file");
        }

        return Skip(count * bytes);
    }

    bool Be32(std::uint32_t & output)
    {
        const std::uint8_t * bytes = nullptr;

        if (!Take(4, bytes))
        {
            return false;
        }

        output = ReadBe32(bytes);
        return true;
    }

    // A WriteBig int that must lie in [0, maximum].
    bool Count(std::uint32_t maximum, std::uint32_t & output)
    {
        if (!Be32(output))
        {
            return false;
        }

        if (output > maximum)
        {
            return Fail("count " + std::to_string(static_cast<std::int32_t>(output)) + " out of range");
        }

        return true;
    }

    bool SignedBe32(std::int32_t & output)
    {
        std::uint32_t bits = 0;

        if (!Be32(bits))
        {
            return false;
        }

        output = static_cast<std::int32_t>(bits);
        return true;
    }

    bool BeFloat(float & output)
    {
        std::uint32_t bits = 0;

        if (!Be32(bits))
        {
            return false;
        }

        output = std::bit_cast<float>(bits);

        if (!std::isfinite(output))
        {
            return Fail("non-finite number");
        }

        return true;
    }

    bool LeFloat(float & output)
    {
        const std::uint8_t * bytes = nullptr;

        if (!Take(4, bytes))
        {
            return false;
        }

        output = std::bit_cast<float>(ReadLe32(bytes));

        if (!std::isfinite(output))
        {
            return Fail("non-finite number");
        }

        return true;
    }

    // A WriteBig bool: one byte, 0 or 1.
    bool Flag(bool & output)
    {
        const std::uint8_t * bytes = nullptr;

        if (!Take(1, bytes))
        {
            return false;
        }

        if (bytes[0] > 1)
        {
            return Fail("invalid flag byte");
        }

        output = bytes[0] != 0;
        return true;
    }

    bool Flags(int count)
    {
        for (int index = 0; index < count; ++index)
        {
            bool ignored = false;

            if (!Flag(ignored))
            {
                return false;
            }
        }

        return true;
    }

    // idFile::WriteString: a little-endian length, then the bytes without a terminator.
    bool String(std::string & output, bool allowEmpty)
    {
        const std::uint8_t * bytes = nullptr;

        if (!Take(4, bytes))
        {
            return false;
        }

        const std::uint32_t length = ReadLe32(bytes);

        if (length > kMaxNameBytes || (length == 0 && !allowEmpty) || !Take(length, bytes))
        {
            return Fail("invalid string");
        }

        output.assign(reinterpret_cast<const char *>(bytes), length);

        for (const char c : output)
        {
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
            {
                return Fail("control character in a string");
            }
        }

        return true;
    }

    // ReadBinaryModel's name, then idRenderModelStatic::LoadBinaryModel.
    bool Model(ProcModel & model, bool & shadow)
    {
        std::string outerName;
        std::uint32_t magic = 0;
        std::uint32_t surfaceCount = 0;

        if (!String(outerName, false) || !Be32(magic))
        {
            return false;
        }

        if (magic != kBrmMagic)
        {
            return Fail("model " + outerName + " is not a version 108 binary model");
        }

        if (!Skip(8) || !Count(kMaxBinaryCount, surfaceCount))
        {
            return false;
        }

        shadow = outerName.starts_with("_prelight_");
        const bool area = outerName.starts_with("_area");

        for (std::uint32_t index = 0; index < surfaceCount; ++index)
        {
            ProcSurface surface;
            bool geometry = false;
            bool vertexData = false;

            if (!Skip(4) || !String(surface.material, true) || !Flag(geometry))
            {
                return false;
            }

            if (geometry && !Surface(outerName, surface, vertexData))
            {
                return false;
            }

            // An area surface must carry what the text format would: a material and its vertices.
            if (area && (surface.material.empty() || !geometry || (!vertexData && !surface.indices.empty())))
            {
                return Fail("area model " + outerName + " has a surface without a material or vertices");
            }

            if (!shadow)
            {
                model.surfaces.push_back(std::move(surface));
            }
        }

        // Bounds, overlaysAdded, the last modified and archived frames, the name and nine flags.
        if (!Skip(24 + 12) || !String(model.name, false) || !Flags(9))
        {
            return false;
        }

        if (model.name != outerName)
        {
            return Fail("model " + outerName + " names itself " + model.name);
        }

        return true;
    }

    // The srfTriangles_t record: positions, texture coordinates and normals are kept, the
    // precomputed shadow and silhouette data are checked for size and skipped.
    bool Surface(const std::string & modelName, ProcSurface & surface, bool & vertexData)
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t inFile = 0;
        std::uint32_t shadowVertexCount = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t count = 0;

        // Bounds, ambientViewCount and four flags.
        if (!Skip(24 + 4) || !Flags(4) || !Count(static_cast<std::uint32_t>(kMaxSurfaceVertices), vertexCount) || !Count(kMaxBinaryCount, inFile))
        {
            return false;
        }

        vertexData = inFile != 0;

        if (vertexData)
        {
            if (vertexCount > (m_data.size() - m_cursor) / kDrawVertBytes)
            {
                return Fail("vertices of " + modelName + " run past the end of the file");
            }

            surface.vertices.resize(vertexCount);

            for (ProcVertex & vertex : surface.vertices)
            {
                const std::uint8_t * bytes = nullptr;

                if (!LeFloat(vertex.position[0]) || !LeFloat(vertex.position[1]) || !LeFloat(vertex.position[2]) || !Take(20, bytes))
                {
                    return false;
                }

                vertex.uv = { HalfToFloat(static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1])),
                              HalfToFloat(static_cast<std::uint16_t>((bytes[2] << 8) | bytes[3])) };
                vertex.normal = { ByteToUnit(bytes[4]), ByteToUnit(bytes[5]), ByteToUnit(bytes[6]) };
                // Tangents, colour and color2 follow; the area vertices' colours are garbage (15.2).
            }
        }

        if (!Count(kMaxBinaryCount, shadowVertexCount) || !SkipArray(shadowVertexCount, 16) || !Count(kMaxBinaryCount, indexCount))
        {
            return false;
        }

        if (indexCount % 3 != 0 || indexCount > (m_data.size() - m_cursor) / 2)
        {
            return Fail("index count of " + modelName + " out of range");
        }

        surface.indices.resize(indexCount);

        for (std::uint32_t & index : surface.indices)
        {
            const std::uint8_t * bytes = nullptr;

            if (!Take(2, bytes))
            {
                return false;
            }

            index = static_cast<std::uint32_t>((bytes[0] << 8) | bytes[1]);

            if (vertexData && index >= vertexCount)
            {
                return Fail("index out of range in " + modelName);
            }
        }

        // Silhouette indexes (one per index when present), mirrored and duplicate vertices and
        // silhouette edges.
        if (!Count(kMaxBinaryCount, count) || (count != 0 && !SkipArray(indexCount, 2)) || !Count(kMaxBinaryCount, count) || !SkipArray(count, 4) ||
            !Count(kMaxBinaryCount, count) || !SkipArray(count, 8) || !Count(kMaxBinaryCount, count) || !SkipArray(count, 8))
        {
            return false;
        }

        // Dominant triangles (one per vertex when present), then three shadow index counts.
        bool dominant = false;

        if (!Flag(dominant) || (dominant && !SkipArray(vertexCount, 16)) || !Skip(12))
        {
            return false;
        }

        return true;
    }

    bool Portals(ProcFile & proc)
    {
        std::uint32_t areaCount = 0;
        std::uint32_t portalCount = 0;

        if (!Count(65535, areaCount) || !Count(kMaxBinaryCount, portalCount))
        {
            return false;
        }

        proc.areaCount = static_cast<int>(areaCount);

        if (portalCount > (m_data.size() - m_cursor) / 12)
        {
            return Fail("portals run past the end of the file");
        }

        proc.portals.resize(portalCount);

        for (ProcPortal & portal : proc.portals)
        {
            std::uint32_t pointCount = 0;
            std::uint32_t areas[2] = {};

            if (!Count(64, pointCount) || !Be32(areas[0]) || !Be32(areas[1]))
            {
                return false;
            }

            if (pointCount < 3 || areas[0] >= areaCount || areas[1] >= areaCount || areas[0] == areas[1])
            {
                return Fail("invalid portal");
            }

            portal.areas[0] = static_cast<int>(areas[0]);
            portal.areas[1] = static_cast<int>(areas[1]);
            portal.points.resize(pointCount);

            for (std::array<float, 3> & point : portal.points)
            {
                if (!BeFloat(point[0]) || !BeFloat(point[1]) || !BeFloat(point[2]))
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool Nodes(ProcFile & proc)
    {
        std::uint32_t nodeCount = 0;

        if (!Count(kMaxBinaryCount, nodeCount))
        {
            return false;
        }

        if (nodeCount > (m_data.size() - m_cursor) / 24)
        {
            return Fail("nodes run past the end of the file");
        }

        proc.nodes.resize(nodeCount);

        for (ProcNode & node : proc.nodes)
        {
            for (float & value : node.plane)
            {
                if (!BeFloat(value))
                {
                    return false;
                }
            }

            for (int & child : node.children)
            {
                std::int32_t value = 0;

                if (!SignedBe32(value))
                {
                    return false;
                }

                if (value < -kMaxCount || value > kMaxCount)
                {
                    return Fail("node child out of range");
                }

                child = value;
            }
        }

        return true;
    }

    std::span<const std::uint8_t> m_data;
    std::size_t m_cursor = 0;
    std::string & m_error;
};

} // namespace

bool ParseProc(std::string_view text, ProcFile & output, std::string & error)
{
    output = {};
    error.clear();
    Parser parser(text, error);
    return parser.File(output);
}

bool ParseBinaryProc(std::span<const std::uint8_t> data, ProcFile & output, std::string & error)
{
    output = {};
    error.clear();
    BinaryParser parser(data, error);
    return parser.File(output);
}

const ProcModel * FindAreaModel(const ProcFile & proc, int area)
{
    const std::string name = "_area" + std::to_string(area);

    for (const ProcModel & model : proc.models)
    {
        if (model.name == name)
        {
            return &model;
        }
    }

    return nullptr;
}

} // namespace assets
