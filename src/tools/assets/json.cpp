// ================================================================================================
// File: json.cpp
// Brief: A small strict JSON reader for conversion jobs, plus string escaping for the reports, so
// the tools exchange structured data with the Python cook without a third-party library.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace assets
{
namespace
{

static constexpr int kMaxDepth = 64;

class Parser
{
public:
    explicit Parser(std::string_view source)
        : m_source(source)
    {
    }

    bool Document(JsonValue & output, std::string & error)
    {
        if (!Value(output, 0))
        {
            error = m_error + " at byte " + std::to_string(m_cursor);
            return false;
        }

        SkipSpace();

        if (m_cursor != m_source.size())
        {
            error = "trailing JSON content at byte " + std::to_string(m_cursor);
            return false;
        }

        return true;
    }

private:
    bool Fail(const char * message)
    {
        m_error = message;
        return false;
    }

    void SkipSpace()
    {
        while (m_cursor < m_source.size() &&
               (m_source[m_cursor] == ' ' || m_source[m_cursor] == '\t' || m_source[m_cursor] == '\n' || m_source[m_cursor] == '\r'))
        {
            ++m_cursor;
        }
    }

    bool Literal(std::string_view word)
    {
        if (m_source.substr(m_cursor, word.size()) != word)
        {
            return false;
        }

        m_cursor += word.size();
        return true;
    }

    bool Value(JsonValue & output, int depth)
    {
        if (depth > kMaxDepth)
        {
            return Fail("JSON nesting too deep");
        }

        SkipSpace();

        if (m_cursor >= m_source.size())
        {
            return Fail("unexpected end of JSON");
        }

        const char next = m_source[m_cursor];

        if (next == '{')
        {
            return Object(output, depth);
        }

        if (next == '[')
        {
            return Array(output, depth);
        }

        if (next == '"')
        {
            output.type = JsonValue::Type::String;
            return String(output.text);
        }

        if (Literal("true"))
        {
            output.type = JsonValue::Type::Boolean;
            output.boolean = true;
            return true;
        }

        if (Literal("false"))
        {
            output.type = JsonValue::Type::Boolean;
            output.boolean = false;
            return true;
        }

        if (Literal("null"))
        {
            output.type = JsonValue::Type::Null;
            return true;
        }

        return Number(output);
    }

    bool Object(JsonValue & output, int depth)
    {
        output.type = JsonValue::Type::Object;
        ++m_cursor;
        SkipSpace();

        if (m_cursor < m_source.size() && m_source[m_cursor] == '}')
        {
            ++m_cursor;
            return true;
        }

        while (true)
        {
            SkipSpace();
            std::string key;

            if (m_cursor >= m_source.size() || m_source[m_cursor] != '"' || !String(key))
            {
                return Fail("JSON object key expected");
            }

            if (output.Find(key) != nullptr)
            {
                return Fail("duplicate JSON object key");
            }

            SkipSpace();

            if (m_cursor >= m_source.size() || m_source[m_cursor] != ':')
            {
                return Fail("JSON ':' expected");
            }

            ++m_cursor;
            JsonValue value;

            if (!Value(value, depth + 1))
            {
                return false;
            }

            output.keys.push_back(std::move(key));
            output.items.push_back(std::move(value));
            SkipSpace();

            if (m_cursor < m_source.size() && m_source[m_cursor] == ',')
            {
                ++m_cursor;
                continue;
            }

            if (m_cursor < m_source.size() && m_source[m_cursor] == '}')
            {
                ++m_cursor;
                return true;
            }

            return Fail("JSON ',' or '}' expected");
        }
    }

    bool Array(JsonValue & output, int depth)
    {
        output.type = JsonValue::Type::Array;
        ++m_cursor;
        SkipSpace();

        if (m_cursor < m_source.size() && m_source[m_cursor] == ']')
        {
            ++m_cursor;
            return true;
        }

        while (true)
        {
            JsonValue value;

            if (!Value(value, depth + 1))
            {
                return false;
            }

            output.items.push_back(std::move(value));
            SkipSpace();

            if (m_cursor < m_source.size() && m_source[m_cursor] == ',')
            {
                ++m_cursor;
                continue;
            }

            if (m_cursor < m_source.size() && m_source[m_cursor] == ']')
            {
                ++m_cursor;
                return true;
            }

            return Fail("JSON ',' or ']' expected");
        }
    }

    bool String(std::string & output)
    {
        ++m_cursor;

        while (m_cursor < m_source.size())
        {
            const char character = m_source[m_cursor++];

            if (character == '"')
            {
                return true;
            }

            if (static_cast<unsigned char>(character) < 0x20)
            {
                return Fail("control character in JSON string");
            }

            if (character != '\\')
            {
                output.push_back(character);
                continue;
            }

            if (m_cursor >= m_source.size())
            {
                break;
            }

            const char escape = m_source[m_cursor++];

            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                output.push_back(escape);
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'u':
            {
                if (m_source.size() - m_cursor < 4)
                {
                    return Fail("truncated JSON escape");
                }

                unsigned value = 0;

                for (int digit = 0; digit < 4; ++digit)
                {
                    const char hex = m_source[m_cursor++];
                    value <<= 4;

                    if (hex >= '0' && hex <= '9')
                    {
                        value |= static_cast<unsigned>(hex - '0');
                    }
                    else if (hex >= 'a' && hex <= 'f')
                    {
                        value |= static_cast<unsigned>(hex - 'a' + 10);
                    }
                    else if (hex >= 'A' && hex <= 'F')
                    {
                        value |= static_cast<unsigned>(hex - 'A' + 10);
                    }
                    else
                    {
                        return Fail("invalid JSON escape digit");
                    }
                }

                if (value > 0x7F)
                {
                    return Fail("non-ASCII JSON escape");
                }

                output.push_back(static_cast<char>(value));
                break;
            }
            default:
                return Fail("invalid JSON escape");
            }
        }

        return Fail("unterminated JSON string");
    }

    bool Number(JsonValue & output)
    {
        const std::size_t start = m_cursor;

        if (m_cursor < m_source.size() && m_source[m_cursor] == '-')
        {
            ++m_cursor;
        }

        const auto Digits = [this]
        {
            const std::size_t first = m_cursor;

            while (m_cursor < m_source.size() && m_source[m_cursor] >= '0' && m_source[m_cursor] <= '9')
            {
                ++m_cursor;
            }

            return m_cursor > first;
        };

        if (!Digits())
        {
            return Fail("JSON value expected");
        }

        if (m_cursor < m_source.size() && m_source[m_cursor] == '.')
        {
            ++m_cursor;

            if (!Digits())
            {
                return Fail("JSON fraction digits expected");
            }
        }

        if (m_cursor < m_source.size() && (m_source[m_cursor] == 'e' || m_source[m_cursor] == 'E'))
        {
            ++m_cursor;

            if (m_cursor < m_source.size() && (m_source[m_cursor] == '+' || m_source[m_cursor] == '-'))
            {
                ++m_cursor;
            }

            if (!Digits())
            {
                return Fail("JSON exponent digits expected");
            }
        }

        const std::string text(m_source.substr(start, m_cursor - start));
        char * end = nullptr;
        output.type = JsonValue::Type::Number;
        output.number = std::strtod(text.c_str(), &end);

        if (end != text.c_str() + text.size() || !std::isfinite(output.number))
        {
            return Fail("invalid JSON number");
        }

        return true;
    }

    std::string_view m_source;
    std::size_t m_cursor = 0;
    std::string m_error;
};

} // namespace

const JsonValue * JsonValue::Find(std::string_view key) const
{
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (keys[index] == key)
        {
            return &items[index];
        }
    }

    return nullptr;
}

bool JsonValue::IsInteger(std::int64_t minimum, std::int64_t maximum) const
{
    return type == Type::Number && std::floor(number) == number && number >= static_cast<double>(minimum) &&
           number <= static_cast<double>(maximum);
}

bool ParseJson(std::string_view source, JsonValue & output, std::string & error)
{
    JsonValue value;
    Parser parser(source);

    if (!parser.Document(value, error))
    {
        return false;
    }

    output = std::move(value);
    return true;
}

std::string JsonString(std::string_view text)
{
    std::string output = "\"";

    for (const char character : text)
    {
        if (character == '"' || character == '\\')
        {
            output.push_back('\\');
            output.push_back(character);
        }
        else if (static_cast<unsigned char>(character) < 0x20)
        {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(character)));
            output += escape;
        }
        else
        {
            output.push_back(character);
        }
    }

    output.push_back('"');
    return output;
}

} // namespace assets
