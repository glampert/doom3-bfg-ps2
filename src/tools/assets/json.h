// ================================================================================================
// File: json.h
// Brief: A small strict JSON reader for conversion jobs, plus string escaping for the reports, so
// the tools exchange structured data with the Python cook without a third-party library.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

class JsonValue final
{
public:
    enum class Type : std::uint8_t
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    // Array elements, or object member values in file order.
    std::vector<JsonValue> items;
    // Object member names, parallel to items.
    std::vector<std::string> keys;

    const JsonValue * Find(std::string_view key) const;
    // True for numbers that are exact integers within [minimum, maximum].
    bool IsInteger(std::int64_t minimum, std::int64_t maximum) const;
};

// Parses one complete document: depth at most 64, no duplicate object keys, ASCII escapes only.
bool ParseJson(std::string_view source, JsonValue & output, std::string & error);

// Quotes and escapes text for a JSON string literal.
std::string JsonString(std::string_view text);

} // namespace assets
