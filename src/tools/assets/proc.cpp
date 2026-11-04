// ================================================================================================
// File: proc.cpp
// Brief: Tokenizer and parser of dmap's text .proc files for the world cook.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/proc.h"

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

        return Check(proc);
    }

    bool Check(ProcFile & proc)
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
                return Fail("missing model _area" + std::to_string(area));
            }
        }

        if (proc.nodes.empty())
        {
            return Fail("no area nodes");
        }

        for (const ProcNode & node : proc.nodes)
        {
            for (const int child : node.children)
            {
                if ((child > 0 && static_cast<std::size_t>(child) >= proc.nodes.size()) || (child < 0 && -1 - child >= proc.areaCount))
                {
                    return Fail("node child out of range");
                }
            }
        }

        return true;
    }

private:
    Tokens m_tokens;
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
