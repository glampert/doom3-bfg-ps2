// ================================================================================================
// File: map_entities.cpp
// Brief: Read the entities of a Doom 3 .map (version 2 or 3): each entity's key/value pairs, with
// its brush and patch primitives counted and skipped, which is what the light cook needs from it.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/map_entities.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace assets
{

static std::string LowerKey(std::string_view text)
{
    std::string lower(text);

    for (char & character : lower)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return lower;
}

// idStr's float parse is atof: leading numbers count, trailing text is ignored.
static bool ParseFloats(const std::string & text, float * output, int count)
{
    const char * at = text.c_str();

    for (int index = 0; index < count; ++index)
    {
        char * end = nullptr;
        output[index] = std::strtof(at, &end);

        if (end == at)
        {
            return false;
        }

        at = end;
    }

    return true;
}

const std::string * MapEntity::Find(std::string_view key) const
{
    for (const auto & [name, value] : keys)
    {
        if (name == key)
        {
            return &value;
        }
    }

    return nullptr;
}

std::string MapEntity::String(std::string_view key, std::string_view fallback) const
{
    const std::string * value = Find(key);
    return value != nullptr ? *value : std::string(fallback);
}

float MapEntity::Float(std::string_view key, float fallback) const
{
    const std::string * value = Find(key);
    // idDict::GetFloat is atof of the value: an unparsable one reads 0.
    return value != nullptr ? std::strtof(value->c_str(), nullptr) : fallback;
}

int MapEntity::Int(std::string_view key, int fallback) const
{
    const std::string * value = Find(key);
    return value != nullptr ? static_cast<int>(std::strtol(value->c_str(), nullptr, 10)) : fallback;
}

bool MapEntity::Bool(std::string_view key, bool fallback) const
{
    const std::string * value = Find(key);
    return value != nullptr ? std::strtol(value->c_str(), nullptr, 10) != 0 : fallback;
}

bool MapEntity::Vector(std::string_view key, std::array<float, 3> & output) const
{
    const std::string * value = Find(key);
    std::array<float, 3> parsed = {};

    if (value == nullptr || !ParseFloats(*value, parsed.data(), 3))
    {
        return false;
    }

    output = parsed;
    return true;
}

bool MapEntity::Matrix(std::string_view key, std::array<std::array<float, 3>, 3> & output) const
{
    const std::string * value = Find(key);
    float parsed[9] = {};

    if (value == nullptr || !ParseFloats(*value, parsed, 9))
    {
        return false;
    }

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            output[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = parsed[row * 3 + column];
        }
    }

    return true;
}

void MapEntity::Placement(std::array<float, 3> & origin, std::array<std::array<float, 3>, 3> & axis) const
{
    origin = {};
    (void)Vector("origin", origin);

    if (Matrix("rotation", axis))
    {
        return;
    }

    const float yaw = Float("angle", 0.0f) * 3.14159265358979323846f / 180.0f;
    axis = { std::array<float, 3>{ std::cos(yaw), std::sin(yaw), 0.0f }, std::array<float, 3>{ -std::sin(yaw), std::cos(yaw), 0.0f },
             std::array<float, 3>{ 0.0f, 0.0f, 1.0f } };
}

bool ParseMapEntities(std::string_view text, std::vector<MapEntity> & output, std::string & error)
{
    output.clear();
    std::size_t at = 0;
    int line = 1;
    int depth = 0;
    MapEntity entity;
    std::string pendingKey;
    std::string pendingWritten;
    bool haveKey = false;

    const auto Fail = [&error, &line](const std::string & message)
    {
        error = message + " at line " + std::to_string(line);
        return false;
    };

    while (at < text.size())
    {
        const char character = text[at];

        if (character == '\n')
        {
            ++line;
            ++at;
        }
        else if (text.compare(at, 2, "//") == 0)
        {
            while (at < text.size() && text[at] != '\n')
            {
                ++at;
            }
        }
        else if (text.compare(at, 2, "/*") == 0)
        {
            const std::size_t end = text.find("*/", at + 2);

            if (end == std::string_view::npos)
            {
                return Fail("unterminated comment");
            }

            for (std::size_t index = at; index < end; ++index)
            {
                line += text[index] == '\n' ? 1 : 0;
            }

            at = end + 2;
        }
        else if (character == '"')
        {
            const std::size_t end = text.find('"', at + 1);

            if (end == std::string_view::npos)
            {
                return Fail("unterminated string");
            }

            const std::string_view value = text.substr(at + 1, end - at - 1);

            for (const char inside : value)
            {
                line += inside == '\n' ? 1 : 0;
            }

            at = end + 1;

            if (depth == 1)
            {
                if (!haveKey)
                {
                    pendingKey = LowerKey(value);
                    pendingWritten = std::string(value);
                    haveKey = true;
                }
                else
                {
                    entity.keys.emplace_back(pendingKey, std::string(value));
                    entity.written.emplace_back(pendingWritten, std::string(value));
                    haveKey = false;
                }
            }
            else if (depth == 0)
            {
                return Fail("a string outside any entity");
            }
        }
        else if (character == '{')
        {
            if (haveKey)
            {
                return Fail("a key without a value");
            }

            if (++depth == 2)
            {
                ++entity.primitives;
            }

            if (depth > 8)
            {
                return Fail("nesting too deep");
            }

            ++at;
        }
        else if (character == '}')
        {
            if (depth == 0 || haveKey)
            {
                return Fail(depth == 0 ? "an unmatched '}'" : "a key without a value");
            }

            if (--depth == 0)
            {
                output.push_back(std::move(entity));
                entity = {};
            }

            ++at;
        }
        else
        {
            // Version lines and primitive contents.
            if (depth == 1 && std::isspace(static_cast<unsigned char>(character)) == 0)
            {
                return Fail("unexpected text in an entity");
            }

            ++at;
        }
    }

    if (depth != 0)
    {
        return Fail("an entity lacks its closing brace");
    }

    return true;
}

} // namespace assets
